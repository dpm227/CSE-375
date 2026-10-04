/*
 * PA1 Part 2
 * Sunlab:
 *   c++ -O3 -std=c++17 -pthread main.cpp -ltbb -o kmeans
 * Local:
 *   c++ -O3 -std=c++17 -pthread main.cpp -I/opt/homebrew/opt/tbb/include -L/opt/homebrew/opt/tbb/lib -Wl,-rpath,/opt/homebrew/opt/tbb/lib -ltbb -o kmeans
 * Run either dataset:
 *   ./kmeans 4 1 256 < datasets/dataset1.txt
 *   ./kmeans 4 1 256 < datasets/dataset2.txt
 * Arguments: threads (0 = sequential), seed, grain size, optional --quiet.
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
        int threads = argc > 1 ? std::stoi(argv[1]) : 1;
        int seed = argc > 2 ? std::stoi(argv[2]) : 1;
        int grain = argc > 3 ? std::stoi(argv[3]) : 256;
        bool quiet = argc > 4 && std::string(argv[4]) == "--quiet";
        int n, d, k, limit, named;
        if (threads < 0 || grain < 1 ||
            !(std::cin >> n >> d >> k >> limit >> named) ||
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
                                    std::max(1, threads));
        auto begin = std::chrono::steady_clock::now();
        std::vector<int> labels(n, -1), selected;
        std::vector<double> centers(k * d);
        std::srand(seed);

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
            Totals totals = threads == 0 ? assign(range, Totals(k, d)) : tbb::parallel_reduce(range, Totals(k, d), assign, combine);

            auto update = [&](int c)
            {
                if (totals.counts[c] != 0)
                    for (int j = 0; j < d; ++j)
                        centers[c * d + j] = totals.sums[c * d + j] / totals.counts[c];
            };
            if (threads == 0)
                for (int c = 0; c < k; ++c)
                    update(c);
            else
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
        if (!quiet)
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
        std::cerr << e.what() << "\nUsage: ./kmeans [threads>=0] [seed] [grain>0] [--quiet] < data.txt\n";
        return 1;
    }
}
