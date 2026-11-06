// PA2 Hash Set Variations

#ifndef PA2_HASH_SET_H
#define PA2_HASH_SET_H

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

// GCC supplies transaction clones for these functions with -fgnu-tm.
#ifdef __cpp_transactional_memory
#define TM_SAFE __attribute__((transaction_safe))
#define TM_PURE __attribute__((transaction_pure))
#else
#define TM_SAFE
#define TM_PURE
#endif

// helpers for all three hash implementations
namespace hash_detail
{
    // max number of moves before giving up, used by SeqHash & StripedHash
    constexpr int LIMIT = 128;

    // StripedHashSet probing
    constexpr int PROBE_SIZE = 4;
    constexpr int THRESHOLD = 2;

    // scramble the hash across the table
    inline std::size_t mix(std::size_t value) TM_SAFE;
    inline std::size_t mix(std::size_t value)
    {
        std::uint64_t x = value;
        x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
        return x ^ (x >> 31);
    }

    template <class T, class Hash>
    struct Entry
    {
        const T value;
        const std::size_t hash[2];
        Entry *next = nullptr;

        explicit Entry(const T &value)
            : value(value), hash{mix(Hash{}(value)),
                                 mix(Hash{}(value) ^ UINT64_C(0x9e3779b97f4a7c15))} {}
    };

    // Keys are immutable and stay alive until the set is destroyed. Equality must
    // only read its arguments (no side effects), so it needs no transaction clone.
    template <class T, class Equal>
    TM_PURE bool equal(const T &a, const T &b)
    {
        return Equal{}(a, b);
    }

    // Generate exactly n successful insertions, rather than counting duplicates.
    // Call only while no other thread is accessing the set. The generator must
    // supply enough distinct values; the attempt limit catches exhausted domains.
    template <class Set, class Generator>
    void populate(Set &set, std::size_t n, Generator generate)
    {
        std::size_t added = 0;
        std::size_t misses = 0;
        while (added < n)
        {
            if (set.add(generate()))
            {
                ++added;
                misses = 0;
            }
            else if (++misses == 1'000'000)
                throw std::runtime_error("populate: generator is not producing new values");
        }
    }
} // namespace hash_detail

// Section 13.4.1: two arrays, one item per slot, alternating evictions.
// Hash and Equal are default-constructible, pure function objects. Equal keys
// must have equal hashes. Custom types can supply their own Hash and Equal.
template <class T, class Hash = std::hash<T>, class Equal = std::equal_to<T>>
class SequentialHashSet
{
protected:
    using Entry = hash_detail::Entry<T, Hash>;
    std::size_t capacity;
    Entry **table;
    Entry *allocated = nullptr;

    TM_SAFE Entry **find(const Entry &key)
    {
        for (int row = 0; row < 2; ++row)
        {
            Entry **slot = &table[row * capacity + key.hash[row] % capacity];
            if (*slot && hash_detail::equal<T, Equal>((*slot)->value, key.value))
                return slot;
        }
        return nullptr;
    }

    // Undo every eviction if a cycle is found, leaving the original set intact.
    TM_SAFE static bool place(Entry **slots, std::size_t count, Entry *item)
    {
        std::size_t path[hash_detail::LIMIT];
        for (int step = 0; step < hash_detail::LIMIT; ++step)
        {
            int row = step % 2;
            std::size_t index = row * count + item->hash[row] % count;
            path[step] = index;
            Entry *displaced = slots[index];
            slots[index] = item;
            item = displaced;
            if (!item)
                return true;
        }
        for (int step = hash_detail::LIMIT - 1; step >= 0; --step)
        {
            Entry *displaced = slots[path[step]];
            slots[path[step]] = item;
            item = displaced;
        }
        return false;
    }

    // Build a replacement before publishing it. A failed allocation or rebuild
    // leaves the old table intact. Doubling exposes more bits of both hashes.
    TM_SAFE bool grow(Entry *item)
    {
        std::size_t count = capacity;
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            if (count > SIZE_MAX / (4 * sizeof(Entry *)))
                return false;
            count *= 2;
            Entry **replacement = static_cast<Entry **>(std::calloc(2 * count, sizeof(Entry *)));
            if (!replacement)
                return false;
            bool fits = place(replacement, count, item);
            for (std::size_t i = 0; fits && i < 2 * capacity; ++i)
                if (table[i])
                    fits = place(replacement, count, table[i]);
            if (fits)
            {
                std::free(table);
                table = replacement;
                capacity = count;
                return true;
            }
            std::free(replacement);
        }
        return false;
    }

    // Return 1 for insertion, 0 for a duplicate, and -1 for failed growth.
    TM_SAFE int insert(Entry *item)
    {
        if (find(*item))
            return 0;
        if (!place(table, capacity, item) && !grow(item))
            return -1;
        item->next = allocated;
        allocated = item;
        return 1;
    }

    TM_SAFE bool erase(const Entry &key)
    {
        Entry **slot = find(key);
        if (!slot)
            return false;
        *slot = nullptr;
        return true;
    }

public:
    explicit SequentialHashSet(std::size_t initialCapacity = 16)
        : capacity(initialCapacity), table(nullptr)
    {
        if (!capacity || capacity > SIZE_MAX / (2 * sizeof(Entry *)))
            throw std::invalid_argument("invalid capacity");
        table = static_cast<Entry **>(std::calloc(2 * capacity, sizeof(Entry *)));
        if (!table)
            throw std::bad_alloc();
    }

    SequentialHashSet(const SequentialHashSet &) = delete;
    SequentialHashSet &operator=(const SequentialHashSet &) = delete;

    // Destruction, size, and populate require all worker threads to be stopped.
    ~SequentialHashSet()
    {
        std::free(table);
        while (allocated)
        {
            Entry *next = allocated->next;
            delete allocated;
            allocated = next;
        }
    }

    bool add(const T &value)
    {
        auto item = std::make_unique<Entry>(value);
        int result = insert(item.get());
        if (result < 0)
            throw std::runtime_error("cuckoo growth failed: allocation or excessive hash collisions");
        if (result)
            item.release();
        return result != 0;
    }

    bool contains(const T &value)
    {
        Entry key(value);
        return find(key) != nullptr;
    }

    bool remove(const T &value)
    {
        Entry key(value);
        return erase(key);
    }

    std::size_t size() const
    {
        std::size_t count = 0;
        for (std::size_t i = 0; i < 2 * capacity; ++i)
            count += table[i] != nullptr;
        return count;
    }

    template <class Generator>
    void populate(std::size_t n, Generator generate)
    {
        hash_detail::populate(*this, n, generate);
    }
};

#ifdef __cpp_transactional_memory
// The sequential operations, including resizing, execute as GCC transactions.
// Construct/hash keys outside the transaction so arbitrary value types work.
template <class T, class Hash = std::hash<T>, class Equal = std::equal_to<T>>
class TransactionalHashSet : private SequentialHashSet<T, Hash, Equal>
{
    using Base = SequentialHashSet<T, Hash, Equal>;
    using Entry = typename Base::Entry;

public:
    using Base::Base;
    using Base::size;

    bool add(const T &value)
    {
        auto item = std::make_unique<Entry>(value);
        Entry *raw = item.get();
        int result;
        __transaction_atomic { result = this->insert(raw); }
        if (result < 0)
            throw std::runtime_error("cuckoo growth failed: allocation or excessive hash collisions");
        if (result)
            item.release();
        return result != 0;
    }

    bool contains(const T &value)
    {
        Entry key(value);
        bool result;
        __transaction_atomic { result = this->find(key) != nullptr; }
        return result;
    }

    bool remove(const T &value)
    {
        Entry key(value);
        bool result;
        __transaction_atomic { result = this->erase(key); }
        return result;
    }

    template <class Generator>
    void populate(std::size_t n, Generator generate)
    {
        hash_detail::populate(*this, n, generate);
    }
};
#endif

// Sections 13.4.2-13.4.3: bounded probe sets and a fixed pair of lock arrays.
template <class T, class Hash = std::hash<T>, class Equal = std::equal_to<T>>
class StripedHashSet
{
    using Entry = hash_detail::Entry<T, Hash>;
    using Item = std::shared_ptr<const Entry>;
    struct Bucket
    {
        Item items[hash_detail::PROBE_SIZE];
        int count = 0;

        void append(const Item &item) { items[count++] = item; }
        void erase(int index)
        {
            for (int i = index + 1; i < count; ++i)
                items[i - 1] = std::move(items[i]);
            items[--count].reset();
        }
    };

    const std::size_t lockCount;
    std::unique_ptr<std::mutex[]> locks;
    std::size_t capacity;
    std::vector<Bucket> table;

    // Every operation takes row 0 before row 1, preventing lock-order cycles.
    auto acquire(const Entry &key)
    {
        std::unique_lock<std::mutex> first(locks[key.hash[0] % lockCount]);
        std::unique_lock<std::mutex> second(locks[lockCount + key.hash[1] % lockCount]);
        return std::make_pair(std::move(first), std::move(second));
    }

    Bucket &bucket(int row, const Entry &key)
    {
        return table[row * capacity + key.hash[row] % capacity];
    }

    bool lookup(const Entry &key, bool remove)
    {
        for (int row = 0; row < 2; ++row)
        {
            Bucket &set = bucket(row, key);
            for (int i = 0; i < set.count; ++i)
                if (Equal{}(set.items[i]->value, key.value))
                {
                    if (remove)
                        set.erase(i);
                    return true;
                }
        }
        return false;
    }

    bool relocate(int row, std::size_t column, std::size_t observedCapacity)
    {
        for (int round = 0; round < hash_detail::LIMIT; ++round)
        {
            Item item;
            {
                // Snapshot under the source lock; shared_ptr keeps the key
                // alive if another thread removes it before we lock its pair.
                std::lock_guard<std::mutex> guard(locks[row * lockCount + column % lockCount]);
                if (capacity != observedCapacity)
                    return true;
                Bucket &source = table[row * capacity + column];
                if (source.count < hash_detail::THRESHOLD)
                    return true;
                item = source.items[0];
            }
            auto guard = acquire(*item);
            if (capacity != observedCapacity)
                return true;
            Bucket &source = bucket(row, *item);
            int index = 0;
            while (index < source.count && source.items[index] != item)
                ++index;
            if (index == source.count)
                continue;
            Bucket &target = bucket(1 - row, *item);
            if (target.count == hash_detail::PROBE_SIZE)
                return false;
            target.append(item);
            source.erase(index);
            if (target.count <= hash_detail::THRESHOLD)
                return true;
            row = 1 - row;
            column = item->hash[row] % capacity;
        }
        return false;
    }

    void grow(std::size_t observedCapacity)
    {
        // Lock both rows in order because relocation may snapshot either row.
        std::vector<std::unique_lock<std::mutex>> guards;
        guards.reserve(2 * lockCount);
        for (std::size_t i = 0; i < 2 * lockCount; ++i)
            guards.emplace_back(locks[i]);
        if (capacity != observedCapacity)
            return;

        std::size_t count = capacity;
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            if (count > table.max_size() / 4)
                throw std::length_error("hash set capacity overflow");
            count *= 2;
            std::vector<Bucket> replacement(2 * count);
            bool fits = true;
            for (const Bucket &old : table)
                for (int i = 0; i < old.count; ++i)
                {
                    const Item &item = old.items[i];
                    Bucket &a = replacement[item->hash[0] % count];
                    Bucket &b = replacement[count + item->hash[1] % count];
                    Bucket &target = a.count <= b.count ? a : b;
                    if (target.count == hash_detail::PROBE_SIZE)
                        fits = false;
                    else
                        target.append(item);
                }
            if (fits)
            {
                table.swap(replacement);
                capacity = count;
                return;
            }
        }
        throw std::runtime_error("cuckoo growth failed: excessive hash collisions");
    }

public:
    // Capacity starts at lockCount and only doubles, so each bucket always
    // belongs to the same stripe even after resizing.
    explicit StripedHashSet(std::size_t stripes = 16)
        : lockCount(stripes), locks(nullptr), capacity(stripes)
    {
        if (!stripes || stripes > table.max_size() / 2)
            throw std::invalid_argument("invalid stripe count");
        locks = std::make_unique<std::mutex[]>(2 * stripes);
        table.resize(2 * stripes);
    }

    bool add(const T &value)
    {
        Item item = std::make_shared<Entry>(value);
        int growths = 0;
        for (;;)
        {
            int row = -1;
            std::size_t column = 0;
            std::size_t observedCapacity;
            {
                auto guard = acquire(*item);
                observedCapacity = capacity;
                if (lookup(*item, false))
                    return false;
                // Prefer a probe set below threshold, then use overflow slots.
                for (int limit : {hash_detail::THRESHOLD, hash_detail::PROBE_SIZE})
                {
                    for (int r = 0; r < 2; ++r)
                    {
                        Bucket &set = bucket(r, *item);
                        if (set.count < limit)
                        {
                            set.append(item);
                            if (limit == hash_detail::THRESHOLD)
                                return true;
                            row = r;
                            column = item->hash[r] % capacity;
                            break;
                        }
                    }
                    if (row != -1)
                        break;
                }
            }
            if (row == -1)
            {
                if (++growths > 8)
                    throw std::runtime_error("cuckoo insertion failed: excessive hash collisions");
                grow(observedCapacity);
                continue;
            }
            if (!relocate(row, column, observedCapacity))
            {
                // Insertion has already succeeded. If optional rebalancing
                // cannot allocate, retain the valid overflow slots and report
                // success so callers can still account for this insertion.
                try
                {
                    grow(observedCapacity);
                }
                catch (const std::exception &)
                {
                }
            }
            return true;
        }
    }

    bool contains(const T &value)
    {
        Entry key(value);
        auto guard = acquire(key);
        return lookup(key, false);
    }

    bool remove(const T &value)
    {
        Entry key(value);
        auto guard = acquire(key);
        return lookup(key, true);
    }

    // These two APIs are intended for use before/after worker threads run.
    std::size_t size() const
    {
        std::size_t count = 0;
        for (const Bucket &set : table)
            count += set.count;
        return count;
    }

    template <class Generator>
    void populate(std::size_t n, Generator generate)
    {
        hash_detail::populate(*this, n, generate);
    }
};

#undef TM_SAFE
#undef TM_PURE
#endif
