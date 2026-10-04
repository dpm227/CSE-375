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
        100'000,
        10'000,
        1'000};

    std::vector<int> threadCounts = {
        1,
        2,
        4,
        8,
        16};

    const int numTrials = 5;

    struct Result
    {
        int N;
        int threads;

        double method1Average;
        double method1Variance;

        double method2Average;
        double method2Variance;

        double method3Average;
        double method3Variance;
    };

    std::vector<Result> results;

    auto calculateAverage = [](const std::vector<long long> &times)
    {
        double total = 0;

        for (long long time : times)
        {
            total += time;
        }

        return total / times.size();
    };

    auto calculateVariance = [](const std::vector<long long> &times,
                                double average)
    {
        double sum = 0;

        for (long long time : times)
        {
            double difference = time - average;
            sum += difference * difference;
        }

        return sum / (times.size() - 1);
    };

    for (int N : nValues)
    {
        for (int numThreads : threadCounts)
        {
            std::vector<long long> method1Times;
            std::vector<long long> method2Times;
            std::vector<long long> method3Times;

            // Method 1: TBB
            for (int trial = 0; trial < numTrials; trial++)
            {
                method1Times.push_back(
                    method1(N, numThreads));
            }

            // Method 2: Static
            for (int trial = 0; trial < numTrials; trial++)
            {
                method2Times.push_back(
                    method2(N, numThreads));
            }

            // Method 3: Dynamic
            for (int trial = 0; trial < numTrials; trial++)
            {
                method3Times.push_back(
                    method3(N, numThreads));
            }

            double method1Average =
                calculateAverage(method1Times);

            double method2Average =
                calculateAverage(method2Times);

            double method3Average =
                calculateAverage(method3Times);

            double method1Variance =
                calculateVariance(method1Times, method1Average);

            double method2Variance =
                calculateVariance(method2Times, method2Average);

            double method3Variance =
                calculateVariance(method3Times, method3Average);

            results.push_back({N,
                               numThreads,

                               method1Average,
                               method1Variance,

                               method2Average,
                               method2Variance,

                               method3Average,
                               method3Variance});
        }
    }

    std::cout << "\n\n========================================\n";
    std::cout << "FINAL RESULTS\n";
    std::cout << "Each result is based on "
              << numTrials << " trials\n";
    std::cout << "Average: microseconds\n";
    std::cout << "Variance: microseconds^2\n";
    std::cout << "========================================\n";

    for (const auto &result : results)
    {
        std::cout << "\nN = " << result.N
                  << ", Threads = " << result.threads << "\n";

        std::cout << "Method 1 (TBB):\n";
        std::cout << "  Average:  "
                  << result.method1Average
                  << " microseconds\n";
        std::cout << "  Variance: "
                  << result.method1Variance
                  << " microseconds^2\n";

        std::cout << "Method 2 (Static):\n";
        std::cout << "  Average:  "
                  << result.method2Average
                  << " microseconds\n";
        std::cout << "  Variance: "
                  << result.method2Variance
                  << " microseconds^2\n";

        std::cout << "Method 3 (Dynamic):\n";
        std::cout << "  Average:  "
                  << result.method3Average
                  << " microseconds\n";
        std::cout << "  Variance: "
                  << result.method3Variance
                  << " microseconds^2\n";
    }

    return 0;
}