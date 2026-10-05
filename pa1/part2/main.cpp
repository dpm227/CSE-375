/**
 * Dylan McClellan
 * dpm227
 * PA1
 * 10-4-26
 *
 * Compilation:
 * g++ -O3 -std=c++17 -pthread -DTBB_PREVIEW_GLOBAL_CONTROL=1 main.cpp -ltbb -o kmeans
 * sed 's/srand (time(NULL));/srand(1);/' ../kmeans-serial.cpp > /tmp/kmeans-serial.cpp
 * g++ -O3 -std=c++17 /tmp/kmeans-serial.cpp -o serial
 *
 * Running:
 * bash run_tests.sh
 * Grain-size tests on MAGIC with 8 threads:
 * bash run_tests.sh x 64 128 256 512 1024 2048 4096
 * Single run: ./kmeans 8 x 64 < datasets/magic.txt
 */

#include <tbb/blocked_range.h>
#include <tbb/global_control.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

struct Totals
{
    std::vector<double> sums;
    std::vector<int> counts;
    int changed = 0;
    Totals(int k, int d) : sums(k * d, 0), counts(k, 0) {}
};

int main(int argc, char **argv)
{
    try
    {
        auto positiveInt = [](const char *text) {
            size_t used;
            int value = std::stoi(text, &used);
            if (value < 1 || text[used] != '\0')
                throw std::runtime_error("Expected a positive integer");
            return value;
        };
        if (argc != 1 && argc != 2 &&
            !(argc == 4 && std::string(argv[2]) == "x"))
            throw std::runtime_error("Invalid arguments");
        int threads = argc > 1 ? positiveInt(argv[1]) : 1;
        int grain = argc == 4 ? positiveInt(argv[3]) : 256;
        int n, d, k, limit, named;
        if (!(std::cin >> n >> d >> k >> limit >> named) ||
            n < 1 || d < 1 || k < 1 || k > n || limit < 1 ||
            (named != 0 && named != 1) || k > std::numeric_limits<int>::max() / d)
            throw std::runtime_error("Invalid arguments or dataset header");

        std::vector<double> points(static_cast<size_t>(n) * d);
        std::vector<std::string> names(n);
        for (int i = 0; i < n; ++i)
        {
            for (int j = 0; j < d; ++j)
                if (!(std::cin >> points[static_cast<size_t>(i) * d + j]) ||
                    !std::isfinite(points[static_cast<size_t>(i) * d + j]))
                    throw std::runtime_error("Invalid or missing point value");
            if (named && !(std::cin >> names[i]))
                throw std::runtime_error("Missing point name");
        }

        tbb::global_control control(tbb::global_control::max_allowed_parallelism,
                                    threads);
        auto begin = std::chrono::steady_clock::now();
        std::vector<int> labels(n, -1), selected;
        std::vector<double> centers(k * d);
        std::srand(1);

        for (int c = 0; c < k; ++c)
        {
            int i;
            do
            {
                i = std::rand() % n;
            } while (std::find(selected.begin(), selected.end(), i) != selected.end());
            selected.push_back(i);
            labels[i] = c;
            std::copy_n(&points[static_cast<size_t>(i) * d], d, &centers[c * d]);
        }
        auto initialized = std::chrono::steady_clock::now();
        int iterations = 0;
        for (; iterations < limit;)
        {
            auto assign = [&](const tbb::blocked_range<int> &range, Totals local)
            {
                for (int i = range.begin(); i < range.end(); ++i)
                {
                    int nearest = 0;
                    double best = std::numeric_limits<double>::infinity();
                    for (int c = 0; c < k; ++c)
                    {
                        double distance = 0;
                        for (int j = 0; j < d; ++j)
                        {
                            double diff = points[static_cast<size_t>(i) * d + j] - centers[c * d + j];
                            distance += diff * diff;
                        }
                        if (distance < best)
                        {
                            best = distance;
                            nearest = c;
                        }
                    }
                    local.changed += labels[i] != nearest;
                    labels[i] = nearest;
                    ++local.counts[nearest];
                    for (int j = 0; j < d; ++j)
                        local.sums[nearest * d + j] += points[static_cast<size_t>(i) * d + j];
                }
                return local;
            };
            auto combine = [](Totals a, const Totals &b)
            {
                a.changed += b.changed;
                for (size_t c = 0; c < a.counts.size(); ++c)
                    a.counts[c] += b.counts[c];
                for (size_t j = 0; j < a.sums.size(); ++j)
                    a.sums[j] += b.sums[j];
                return a;
            };
            tbb::blocked_range<int> range(0, n, grain);
            Totals totals = tbb::parallel_reduce(range, Totals(k, d), assign, combine);

            auto update = [&](int c)
            {
                if (totals.counts[c] != 0)
                    for (int j = 0; j < d; ++j)
                        centers[c * d + j] = totals.sums[c * d + j] / totals.counts[c];
            };
            tbb::parallel_for(0, k, update);
            ++iterations;
            if (totals.changed == 0)
                break;
        }
        auto end = std::chrono::steady_clock::now();
        auto us = [](auto a, auto b)
        {
            return std::chrono::duration<double, std::micro>(b - a).count();
        };
        std::cout << std::setprecision(17);
        for (int c = 0; c < k; ++c)
        {
            std::cout << "Cluster " << c + 1 << '\n';
            for (int i = 0; i < n; ++i)
                if (labels[i] == c)
                {
                    std::cout << "Point " << i + 1 << ": ";
                    for (int j = 0; j < d; ++j)
                        std::cout << points[static_cast<size_t>(i) * d + j] << ' ';
                    if (named)
                        std::cout << "- " << names[i];
                    std::cout << '\n';
                }
            std::cout << "Cluster values: ";
            for (int j = 0; j < d; ++j)
                std::cout << centers[c * d + j] << ' ';
            std::cout << '\n';
        }
        std::cout << "Break in iteration " << iterations << '\n'
                  << "TOTAL EXECUTION TIME = " << us(begin, end) << '\n'
                  << "TIME PHASE 1 = " << us(begin, initialized) << '\n'
                  << "TIME PHASE 2 = " << us(initialized, end) << '\n';
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << "\nUsage: ./kmeans [threads] [x grain_size] < data.txt\n";
        return 1;
    }
}
