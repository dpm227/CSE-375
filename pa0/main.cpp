#include <iostream>
#include <map>
#include <random>
#include <chrono>
#include <thread>
#include <vector>
#include <cstdlib>

std::mutex accounts_mutex;

void transfer(std::map<int, float> &accounts, float amount)
{
    std::random_device rd;
    std::mt19937 gen(rd());

    // inclusive range [0, x]
    std::uniform_int_distribution<> dist(0, accounts.size() - 1);

    int a1 = dist(gen);
    int a2 = dist(gen);

    while (a1 == a2)
        a2 = dist(gen);

    std::lock_guard<std::mutex> lock(accounts_mutex);

    accounts[a1] -= amount;
    accounts[a2] += amount;
}

float balance(std::map<int, float> &accounts)
{
    float balance = 0;

    std::lock_guard<std::mutex> lock(accounts_mutex);

    for (int i = 0; i < 10; i++)
        balance += accounts[i];

    return balance;
}

long long do_work(std::map<int, float> &accounts)
{
    int x = 1;

    int threshold = 30;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 1000; i++)
    {
        if (x < threshold)
            transfer(accounts, 100);
        else
            balance(accounts); // make sure every invocation returns SUM_BALANCE
        x++;
        // std::cout << balance(accounts) << std::endl;
    }

    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        end - start);

    return duration.count();
}

int main(int argc, char *argv[])
{
    std::map<int, float> accounts;

    for (int i = 0; i < 10; i++)
        accounts.insert({i, 1000});

    // std::cout << do_work(accounts) << std::endl;

    // transfer(accounts, 100);

    // for (int i = 0; i < 10; i++)
    //     std::cout << accounts[i] << std::endl;

    // std::cout << balance(accounts) << std::endl;

    int num_threads = std::atoi(argv[1]);

    std::vector<std::thread> threads;
    std::vector<long long> exec_times(num_threads);

    for (int i = 0; i < num_threads; i++)
    {
        // create each thread and run do work
        // std::ref ensures the same map is used by each thread
        threads.emplace_back([&, i]()
                             { exec_times[i] = do_work(accounts); });
    }

    // wait for threads to finish
    for (auto &thread : threads)
    {
        thread.join();
    }

    for (int i = 0; i < num_threads; i++)
    {
        std::cout << "Thread" << i << ": " << exec_times[i] << " microseconds\n";
    }

    std::cout << "Final balance: " << balance(accounts) << std::endl;

    return 0;
}