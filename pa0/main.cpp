/**
 * Dylan McClellan
 * dpm227
 * PA0
 * 9-4-26
 * Note: Using C++ 17
 * Run like this: ./main x
 * Where x is the desired number of threads (1,2,3, 4, or 8)
 */

#include <iostream>
#include <map>
#include <random>
#include <chrono>
#include <thread>
#include <vector>
#include <cstdlib>
#include <array>
#include <mutex>
#include <shared_mutex>
#include <algorithm>

constexpr int NUM_ACCOUNTS = 1000;
constexpr int TOTAL_ITERATIONS = 80000;
constexpr int TRANSFER_PERCENTAGE = 75;
constexpr float INITIAL_ACCOUNT_BALANCE = 1000.0f;
constexpr float SUM_BALANCE = NUM_ACCOUNTS * INITIAL_ACCOUNT_BALANCE;

std::array<std::shared_mutex, NUM_ACCOUNTS> account_mutexes;

std::mt19937 &get_random_generator()
{
    static thread_local std::mt19937 generator(std::random_device{}());
    return generator;
}

// std::mutex accounts_mutex;

void transfer(std::map<int, float> &accounts, float amount)
{
    // inclusive range [0, x]
    std::uniform_int_distribution<> dist(0, NUM_ACCOUNTS - 1);

    int a1 = dist(get_random_generator());
    int a2 = dist(get_random_generator());

    while (a1 == a2)
        a2 = dist(get_random_generator());

    // std::lock_guard<std::mutex> lock(accounts_mutex);
    // lock the lower account number first
    int first = std::min(a1, a2);
    int second = std::max(a1, a2);

    // accounts[a1] -= amount;
    // accounts[a2] += amount;
    // transfers with exclusive access to both accounts
    std::unique_lock<std::shared_mutex> first_lock(account_mutexes[first]);
    std::unique_lock<std::shared_mutex> second_lock(account_mutexes[second]);

    accounts.at(a1) -= amount;
    accounts.at(a2) += amount;
}

float balance(const std::map<int, float> &accounts)
{
    // shared lock for every account until the sum is complete
    std::vector<std::shared_lock<std::shared_mutex>> locks;
    locks.reserve(NUM_ACCOUNTS);

    // lock in account number order to prevent deadlock
    for (int i = 0; i < NUM_ACCOUNTS; i++)
        locks.emplace_back(account_mutexes[i]);

    float balance = 0;

    for (const auto &[account_id, account_balance] : accounts)
        balance += account_balance;

    /*
    std::lock_guard<std::mutex> lock(accounts_mutex);

    for (int i = 0; i < 10; i++)
        balance += accounts[i];
    */

    return balance;
}

long long do_work(std::map<int, float> &accounts, int iterations)
{
    std::uniform_int_distribution<int> operation_dist(1, 100);

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; i++)
    {
        int rnd = operation_dist(get_random_generator());

        if (rnd <= TRANSFER_PERCENTAGE)
            transfer(accounts, 100);
        else
        {
            float current_balance = balance(accounts);

            if (current_balance != SUM_BALANCE)
                std::cerr << "Incorrect balance: " << current_balance << "\n";

            // std::cout << balance(accounts) << std::endl;
        }
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    return duration.count();
}

void transfer_sequential(std::map<int, float> &accounts, float amount)
{
    std::uniform_int_distribution<int> dist(0, NUM_ACCOUNTS - 1);

    int a1 = dist(get_random_generator());
    int a2 = dist(get_random_generator());

    while (a1 == a2)
        a2 = dist(get_random_generator());

    accounts.at(a1) -= amount;
    accounts.at(a2) += amount;
}

float balance_sequential(
    const std::map<int, float> &accounts)
{
    float total = 0;

    for (const auto &[account_id, account_balance] : accounts)
        total += account_balance;

    return total;
}

long long do_work_sequential(std::map<int, float> &accounts, int iterations)
{
    std::uniform_int_distribution<int> operation_dist(1, 100);

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; i++)
    {
        int rnd = operation_dist(get_random_generator());

        if (rnd <= TRANSFER_PERCENTAGE)
            transfer_sequential(accounts, 100);
        else
        {
            float current_balance = balance_sequential(accounts);

            if (current_balance != SUM_BALANCE)
                std::cerr << "Incorrect balance: " << current_balance << '\n';
        }
    }

    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    return duration.count();
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0]
                  << " <number_of_threads>\n";
        return 1;
    }

    int num_threads = std::atoi(argv[1]);

    if (num_threads != 1 && num_threads != 2 && num_threads != 4 && num_threads != 8)
    {
        std::cerr << "Number of threads must be 1, 2, 4, or 8.\n";
        return 1;
    }

    int iterations_per_thread = TOTAL_ITERATIONS / num_threads;

    std::map<int, float> accounts;

    for (int i = 0; i < NUM_ACCOUNTS; i++)
        accounts.insert({i, INITIAL_ACCOUNT_BALANCE});

    if (num_threads == 1)
    {
        long long sequential_time =
            do_work_sequential(accounts, TOTAL_ITERATIONS);

        std::cout << "Sequential execution time: "
                  << sequential_time
                  << " microseconds\n";

        std::cout << "Final balance: "
                  << balance_sequential(accounts)
                  << '\n';

        while (!accounts.empty())
            accounts.erase(accounts.begin());

        return 0;
    }

    // std::cout << do_work(accounts) << std::endl;

    // transfer(accounts, 100);

    // for (int i = 0; i < 10; i++)
    //     std::cout << accounts[i] << std::endl;

    // std::cout << balance(accounts) << std::endl;

    std::vector<std::thread> threads;
    std::vector<long long> exec_times(num_threads);

    auto parallel_start =
        std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_threads; i++)
    {
        // create each thread and run do work
        threads.emplace_back([&, i]()
                             { exec_times[i] = do_work(accounts, iterations_per_thread); });
    }

    // wait for threads to finish
    for (auto &thread : threads)
    {
        thread.join();
    }

    auto parallel_end =
        std::chrono::high_resolution_clock::now();

    auto parallel_duration =
        std::chrono::duration_cast<std::chrono::microseconds>(
            parallel_end - parallel_start);

    for (int i = 0; i < num_threads; i++)
    {
        std::cout << "Thread" << i << ": " << exec_times[i] << " microseconds\n";
    }

    std::cout << "Total parallel execution time: "
              << parallel_duration.count()
              << " microseconds\n";

    std::cout << "Final balance: " << balance(accounts) << std::endl;

    while (!accounts.empty())
        accounts.erase(accounts.begin());

    return 0;
}