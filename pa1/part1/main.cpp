/**
 * Dylan McClellan
 * dpm227
 * PA1
 * 10-2-26
 */

#include <iostream>
#include <tbb/parallel_for.h>
#include <tbb/global_control.h>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>

bool isPrime(int n)
{
    if (n <= 1)
    {
        return false;
    }

    for (int i = 2; i < n; ++i)
    {
        if (n % i == 0)
            return false; // found divisor, so it's not prime
    }
    return true;
}

// Method 0: Loop from 1 to n
void method0(int start, int end)
{
    for (int i = start; i <= end; i++)
    {
        if (isPrime(i))
            std::cout << i << " ";
    }
}

// Method 1: TBB parallel_for
long long method1(int N, int num_threads)
{
    tbb::global_control control(
        tbb::global_control::max_allowed_parallelism,
        num_threads);

    auto start = std::chrono::high_resolution_clock::now();

    tbb::parallel_for(1, N + 1, [](int i)
                      {
        if (isPrime(i))
        {
            std::cout << i << " ";
        } });

    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    return duration.count();
}

// Method 2: std::thread with static load
long long method2(int N, int num_threads)
{
    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;

    int chunkSize = N / num_threads;

    for (int i = 0; i < num_threads; i++)
    {
        int rangeStart = i * chunkSize + 1;
        int rangeEnd = (i + 1) * chunkSize;

        if (i == num_threads - 1)
            rangeEnd = N;

        threads.emplace_back(method0, rangeStart, rangeEnd);
    }

    for (auto &thread : threads)
    {
        thread.join();
    }

    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    return duration.count();
}

long long method3(int N, int num_threads)
{
    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    std::atomic<int> nextNumber{1};

    for (int i = 0; i < num_threads; i++)
    {
        threads.emplace_back([&]()
                             {
            while (true)
            {
                int number = nextNumber.fetch_add(1);

                if (number > N)
                    break;
                
                if(isPrime(number))
                    std::cout << number << " ";
            } });
    }

    for (auto &thread : threads)
    {
        thread.join();
    }

    auto end = std::chrono::high_resolution_clock::now();

    auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    return duration.count();
}

int main(int argc, char *argv[])
{
    std::vector<int> nValues = {
        1'000,
        10'000,
        100'000};

    std::vector<int> threadCounts = {
        1,
        2,
        4,
        8,
        16};

    const int numTrials = 5;

    for (int N : nValues)
    {
        std::cout << "\n\n========================================\n";
        std::cout << "N = " << N << "\n";
        std::cout << "========================================\n";

        for (int numThreads : threadCounts)
        {
            std::cout << "\n----------------------------------------\n";
            std::cout << "Threads = " << numThreads << "\n";
            std::cout << "----------------------------------------\n";

            long long method1Total = 0;
            long long method2Total = 0;
            long long method3Total = 0;

            // Method 1: TBB
            for (int trial = 0; trial < numTrials; trial++)
            {
                std::cout << "\nMethod 1, Trial " << trial + 1 << ":\n";

                long long time = method1(N, numThreads);
                method1Total += time;

                std::cout << "\nTime: " << time << " microseconds\n";
            }

            // Method 2: Static load balancing
            for (int trial = 0; trial < numTrials; trial++)
            {
                std::cout << "\nMethod 2, Trial " << trial + 1 << ":\n";

                long long time = method2(N, numThreads);
                method2Total += time;

                std::cout << "\nTime: " << time << " microseconds\n";
            }

            // Method 3: Dynamic load balancing
            for (int trial = 0; trial < numTrials; trial++)
            {
                std::cout << "\nMethod 3, Trial " << trial + 1 << ":\n";

                long long time = method3(N, numThreads);
                method3Total += time;

                std::cout << "\nTime: " << time << " microseconds\n";
            }

            double method1Average =
                method1Total / static_cast<double>(numTrials);

            double method2Average =
                method2Total / static_cast<double>(numTrials);

            double method3Average =
                method3Total / static_cast<double>(numTrials);

            std::cout << "\nAVERAGES\n";
            std::cout << "N: " << N
                      << ", Threads: " << numThreads << "\n";

            std::cout << "Method 1 (TBB):     "
                      << method1Average << " microseconds\n";

            std::cout << "Method 2 (Static):  "
                      << method2Average << " microseconds\n";

            std::cout << "Method 3 (Dynamic): "
                      << method3Average << " microseconds\n";
        }
    }

    return 0;
}