/**
 * PA2 Experiment: compare sequential, striped, and transactional cuckoo hash sets
 *
 * running on sunlab:
   module load gcc-11.2.0
   g++ --version
   cd pa2
   g++ -O3 -std=c++17 -pthread -fgnu-tm main.cpp -o benchmark
   ./benchmark > results.csv
 * Use module avail to check for a newer GCC module
 *
 * running locally:
   clang++ -O3 -std=c++17 -pthread main.cpp -o benchmark
   ./benchmark > results-local.csv
 * Clang runs sequential and striped only; it does not support GCC TM
 *
 * Default: 1,000,000 operations, 10,000 initial values, 5 trials per configuration.
 * Optional smaller smoke run (same mixes, thread counts, and trial count):
   ./benchmark 10000 > smoke.csv
 * Output: CSV averages, sample standard deviations, throughput, and speedup.
 */

#include "hash_set.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <exception>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <thread>

constexpr int INITIAL_SIZE = 10'000;
constexpr int KEY_RANGE = 20'000;
constexpr int TRIALS = 5;

struct Operation
{
    int kind; // 0 = contains, 1 = add, 2 = remove
    int key;
};

struct Counts
{
    long long added = 0, removed = 0, found = 0;
    std::exception_ptr error;
};

struct Result
{
    double seconds;
    long long found;
};

template <class Set>
Result run(const std::vector<int> &initial, const std::vector<Operation> &work,
           int threadCount, bool sequential)
{
    Set set; // Each trial starts with a fresh table and the same initial keys
    std::size_t next = 0;
    set.populate(initial.size(), [&]()
                 { return initial[next++]; });
    std::vector<Counts> counts(threadCount);

    auto execute = [&](int t)
    {
        Counts local;
        try
        {
            // Partition the same operation sequence, including any remainder
            std::size_t begin = work.size() * t / threadCount;
            std::size_t end = work.size() * (t + 1) / threadCount;
            for (std::size_t i = begin; i < end; ++i)
            {
                const auto &op = work[i];
                if (op.kind == 0)
                    local.found += set.contains(op.key);
                else if (op.kind == 1)
                    local.added += set.add(op.key);
                else
                    local.removed += set.remove(op.key);
            }
        }
        catch (...)
        {
            local.error = std::current_exception();
        }
        counts[t] = local; // Each thread writes only its own result
    };

    using Clock = std::chrono::steady_clock;
    Clock::time_point start, finish;
    if (sequential)
    {
        start = Clock::now();
        execute(0);
        finish = Clock::now();
    }
    else
    {
        // Prepare all workers before timing, then release them together
        std::mutex mutex;
        std::condition_variable ready, go;
        int waiting = 0;
        bool started = false;
        std::vector<std::thread> workers;
        for (int t = 0; t < threadCount; ++t)
            workers.emplace_back([&, t]()
                                 {
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    ++waiting;
                    ready.notify_one();
                    go.wait(lock, [&]() { return started; });
                }
                execute(t); });
        {
            std::unique_lock<std::mutex> lock(mutex);
            ready.wait(lock, [&]()
                       { return waiting == threadCount; });
            start = Clock::now();
            started = true;
        }
        go.notify_all();
        for (auto &worker : workers)
            worker.join();
        finish = Clock::now(); // Includes worker release and completion overhead
    }

    // Verify the required final size outside the measured interval
    long long expected = initial.size(), found = 0;
    for (const auto &count : counts)
    {
        if (count.error)
            std::rethrow_exception(count.error);
        expected += count.added - count.removed;
        found += count.found;
    }
    if (expected < 0 || set.size() != static_cast<std::size_t>(expected))
        throw std::runtime_error("final size does not match successful updates");
    return {std::chrono::duration<double>(finish - start).count(), found};
}

int main(int argc, char *argv[])
{
    try
    {
        std::size_t operations = 1'000'000;
        if (argc > 2)
            throw std::invalid_argument("usage: ./benchmark [operations]");
        if (argc == 2)
        {
            std::string argument(argv[1]);
            if (argument.empty() || argument.find_first_not_of("0123456789") != std::string::npos)
                throw std::invalid_argument("operations must be a positive integer");
            operations = std::stoull(argument);
            if (!operations)
                throw std::invalid_argument("operations must be positive");
        }
#ifndef __cpp_transactional_memory
        std::cerr << "GCC TM unavailable: running sequential and striped only.\n";
#endif
        // Shuffle unique keys to get the same random initial population each ru
        std::mt19937 initialRandom(1);
        std::vector<int> initial(KEY_RANGE);
        std::iota(initial.begin(), initial.end(), 0);
        std::shuffle(initial.begin(), initial.end(), initialRandom);
        initial.resize(INITIAL_SIZE);

        std::cout << "contains_pct,add_pct,remove_pct,implementation,threads,operations,"
                     "trials,mean_ms,sd_ms,ops_per_second,speedup,contains_hits\n";
        std::cout << std::fixed << std::setprecision(3);
        for (int containsPercent : {80, 50, 10})
        {
            int addPercent = (100 - containsPercent) / 2;
            // Exactly the requested proportions for the default operation count
            // Shuffle operation kinds and assign random keys before timing
            std::mt19937 random(2);
            std::uniform_int_distribution<int> key(0, KEY_RANGE - 1);
            std::vector<Operation> work(operations);
            for (std::size_t i = 0; i < operations; ++i)
                work[i] = {i % 100 < static_cast<unsigned>(containsPercent) ? 0 : i % 100 < static_cast<unsigned>(containsPercent + addPercent) ? 1
                                                                                                                                                : 2,
                           key(random)};
            std::shuffle(work.begin(), work.end(), random);
            double baseline = 0;
            auto measure = [&](auto makeRun, const char *name, int threads)
            {
                double times[TRIALS], sum = 0, squared = 0;
                long long found = 0;
                for (int trial = 0; trial < TRIALS; ++trial)
                {
                    Result result = makeRun();
                    times[trial] = result.seconds;
                    sum += result.seconds;
                    found += result.found;
                }
                double mean = sum / TRIALS;
                for (double time : times)
                    squared += (time - mean) * (time - mean);
                if (!baseline)
                    baseline = mean;
                std::cout << containsPercent << ',' << addPercent << ',' << addPercent << ','
                          << name << ',' << threads << ',' << operations << ',' << TRIALS << ','
                          << mean * 1000 << ',' << std::sqrt(squared / (TRIALS - 1)) * 1000 << ','
                          << operations / mean << ',' << baseline / mean << ',' << found << '\n';
            };
            measure([&]()
                    { return run<SequentialHashSet<int>>(initial, work, 1, true); }, "sequential", 1);
            for (int threads : {1, 2, 4, 8, 16})
            {
                measure([&]()
                        { return run<StripedHashSet<int>>(initial, work, threads, false); }, "striped", threads);
#ifdef __cpp_transactional_memory
                measure([&]()
                        { return run<TransactionalHashSet<int>>(initial, work, threads, false); }, "transactional", threads);
#endif
            }
        }
    }
    catch (const std::exception &error)
    {
        std::cerr << "Benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
