/**
 * @file bench_perfect_hash_compare.cpp
 * @brief Comprehensive benchmarks comparing perfect hash algorithms
 */

#include <maph/core.hpp>
#include <maph/hashers_perfect.hpp>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <random>
#include <algorithm>
#include <numeric>

using namespace maph;
using namespace std::chrono;

// ===== UTILITIES =====

std::vector<std::string> generate_keys(size_t count, size_t min_len = 8, size_t max_len = 32) {
    std::vector<std::string> keys;
    keys.reserve(count);

    std::mt19937_64 rng{42};
    std::uniform_int_distribution<size_t> len_dist(min_len, max_len);
    std::uniform_int_distribution<int> char_dist('a', 'z');

    for (size_t i = 0; i < count; ++i) {
        std::string key;
        size_t len = len_dist(rng);
        key.reserve(len);
        for (size_t j = 0; j < len; ++j) {
            key += static_cast<char>(char_dist(rng));
        }
        keys.push_back(std::move(key));
    }

    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    return keys;
}

template<typename Duration>
double to_milliseconds(Duration d) {
    return duration_cast<microseconds>(d).count() / 1000.0;
}

template<typename Duration>
double to_nanoseconds(Duration d) {
    return duration_cast<nanoseconds>(d).count() * 1.0;
}

// ===== BENCHMARK RUNNER =====

template<typename Hasher, typename Builder>
struct BenchmarkResult {
    std::string algorithm;
    size_t key_count;
    double build_time_ms;
    double avg_query_time_ns;
    double p50_query_time_ns;
    double p95_query_time_ns;
    double p99_query_time_ns;
    size_t memory_bytes;
    double bits_per_key;
    double throughput_mops;  // Million ops per second
};

template<typename BuilderType, typename HasherType>
BenchmarkResult<HasherType, BuilderType> benchmark_algorithm(
    const std::string& name,
    BuilderType builder,
    const std::vector<std::string>& keys,
    size_t query_iterations = 1000000
) {
    BenchmarkResult<HasherType, BuilderType> result;
    result.algorithm = name;
    result.key_count = keys.size();

    // Benchmark build time
    auto build_start = high_resolution_clock::now();
    auto hasher_result = builder.build();
    auto build_end = high_resolution_clock::now();

    if (!hasher_result.has_value()) {
        std::cerr << "Failed to build " << name << std::endl;
        return result;
    }

    auto hasher = std::move(hasher_result.value());
    result.build_time_ms = to_milliseconds(build_end - build_start);

    // Get statistics
    auto stats = hasher.statistics();
    result.memory_bytes = stats.memory_bytes;
    result.bits_per_key = stats.bits_per_key;

    // Benchmark query time
    std::mt19937_64 rng{123};
    std::uniform_int_distribution<size_t> key_dist(0, keys.size() - 1);

    std::vector<double> query_times;
    query_times.reserve(query_iterations);

    auto query_start = high_resolution_clock::now();

    for (size_t i = 0; i < query_iterations; ++i) {
        const auto& key = keys[key_dist(rng)];

        auto q_start = high_resolution_clock::now();
        auto slot = hasher.slot_for(key);
        auto q_end = high_resolution_clock::now();

        // Force usage
        if (!slot.has_value()) {
            std::cerr << "Key not found!" << std::endl;
        }

        query_times.push_back(to_nanoseconds(q_end - q_start));
    }

    auto query_end = high_resolution_clock::now();

    // Calculate statistics
    std::sort(query_times.begin(), query_times.end());

    result.avg_query_time_ns = std::accumulate(query_times.begin(), query_times.end(), 0.0) / query_times.size();
    result.p50_query_time_ns = query_times[query_times.size() / 2];
    result.p95_query_time_ns = query_times[static_cast<size_t>(query_times.size() * 0.95)];
    result.p99_query_time_ns = query_times[static_cast<size_t>(query_times.size() * 0.99)];

    auto total_query_time = to_milliseconds(query_end - query_start);
    result.throughput_mops = (query_iterations / 1000000.0) / (total_query_time / 1000.0);

    return result;
}

void print_header() {
    std::cout << std::left
              << std::setw(15) << "Algorithm"
              << std::setw(10) << "Keys"
              << std::setw(12) << "Build(ms)"
              << std::setw(12) << "Avg(ns)"
              << std::setw(12) << "p50(ns)"
              << std::setw(12) << "p95(ns)"
              << std::setw(12) << "p99(ns)"
              << std::setw(12) << "Bits/Key"
              << std::setw(12) << "MOPS"
              << std::endl;
    std::cout << std::string(120, '-') << std::endl;
}

template<typename HasherType, typename BuilderType>
void print_result(const BenchmarkResult<HasherType, BuilderType>& r) {
    std::cout << std::left << std::fixed << std::setprecision(2)
              << std::setw(15) << r.algorithm
              << std::setw(10) << r.key_count
              << std::setw(12) << r.build_time_ms
              << std::setw(12) << r.avg_query_time_ns
              << std::setw(12) << r.p50_query_time_ns
              << std::setw(12) << r.p95_query_time_ns
              << std::setw(12) << r.p99_query_time_ns
              << std::setw(12) << r.bits_per_key
              << std::setw(12) << r.throughput_mops
              << std::endl;
}

// ===== MAIN BENCHMARK =====

int main(int argc, char** argv) {
    // Parse arguments
    std::vector<size_t> key_counts = {100, 1000, 10000, 100000};
    size_t query_iterations = 1000000;

    if (argc > 1) {
        key_counts.clear();
        for (int i = 1; i < argc; ++i) {
            key_counts.push_back(std::stoul(argv[i]));
        }
    }

    std::cout << "╔════════════════════════════════════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                         Perfect Hash Algorithm Comparison Benchmark                                ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════════════════════════════════════════╝\n";
    std::cout << std::endl;

    for (size_t key_count : key_counts) {
        std::cout << "\n=== Benchmark with " << key_count << " keys ===" << std::endl;
        std::cout << std::endl;

        auto keys = generate_keys(key_count);
        std::cout << "Generated " << keys.size() << " unique keys" << std::endl;
        std::cout << std::endl;

        print_header();

        // RecSplit with leaf size 8
        {
            auto builder = recsplit8::builder{};
            for (const auto& key : keys) {
                builder.add(key);
            }
            auto result = benchmark_algorithm<recsplit8::builder, recsplit8>(
                "RecSplit-8", std::move(builder), keys, query_iterations
            );
            print_result(result);
        }

        // RecSplit with leaf size 16
        {
            auto builder = recsplit16::builder{};
            for (const auto& key : keys) {
                builder.add(key);
            }
            auto result = benchmark_algorithm<recsplit16::builder, recsplit16>(
                "RecSplit-16", std::move(builder), keys, query_iterations
            );
            print_result(result);
        }

        // CHD with lambda = 3.0
        {
            auto builder = chd_hasher::builder{};
            for (const auto& key : keys) {
                builder.add(key);
            }
            builder.with_lambda(3.0);
            auto result = benchmark_algorithm<chd_hasher::builder, chd_hasher>(
                "CHD-3.0", std::move(builder), keys, query_iterations
            );
            print_result(result);
        }

        // CHD with lambda = 5.0 (default)
        {
            auto builder = chd_hasher::builder{};
            for (const auto& key : keys) {
                builder.add(key);
            }
            builder.with_lambda(5.0);
            auto result = benchmark_algorithm<chd_hasher::builder, chd_hasher>(
                "CHD-5.0", std::move(builder), keys, query_iterations
            );
            print_result(result);
        }

        // CHD with lambda = 7.0
        {
            auto builder = chd_hasher::builder{};
            for (const auto& key : keys) {
                builder.add(key);
            }
            builder.with_lambda(7.0);
            auto result = benchmark_algorithm<chd_hasher::builder, chd_hasher>(
                "CHD-7.0", std::move(builder), keys, query_iterations
            );
            print_result(result);
        }

        // BBHash with gamma = 2.0
        {
            auto builder = bbhash3::builder{};
            for (const auto& key : keys) {
                builder.add(key);
            }
            builder.with_gamma(2.0);
            auto result = benchmark_algorithm<bbhash3::builder, bbhash3>(
                "BBHash-2.0", std::move(builder), keys, query_iterations
            );
            print_result(result);
        }

        // BBHash with gamma = 2.5
        {
            auto builder = bbhash3::builder{};
            for (const auto& key : keys) {
                builder.add(key);
            }
            builder.with_gamma(2.5);
            auto result = benchmark_algorithm<bbhash3::builder, bbhash3>(
                "BBHash-2.5", std::move(builder), keys, query_iterations
            );
            print_result(result);
        }

        // PTHash with alpha = 98
        {
            auto builder = pthash98::builder{};
            for (const auto& key : keys) {
                builder.add(key);
            }
            auto result = benchmark_algorithm<pthash98::builder, pthash98>(
                "PTHash-98", std::move(builder), keys, query_iterations
            );
            print_result(result);
        }

        // FCH with bucket size = 4.0
        {
            auto builder = fch_hasher::builder{};
            for (const auto& key : keys) {
                builder.add(key);
            }
            builder.with_bucket_size(4.0);
            auto result = benchmark_algorithm<fch_hasher::builder, fch_hasher>(
                "FCH-4.0", std::move(builder), keys, query_iterations
            );
            print_result(result);
        }

        // FCH with bucket size = 6.0
        {
            auto builder = fch_hasher::builder{};
            for (const auto& key : keys) {
                builder.add(key);
            }
            builder.with_bucket_size(6.0);
            auto result = benchmark_algorithm<fch_hasher::builder, fch_hasher>(
                "FCH-6.0", std::move(builder), keys, query_iterations
            );
            print_result(result);
        }

        std::cout << std::endl;
    }

    std::cout << "\n=== Summary and Recommendations ===" << std::endl;
    std::cout << std::endl;
    std::cout << "RecSplit-8:   Best space efficiency (~2 bits/key), fast queries" << std::endl;
    std::cout << "RecSplit-16:  Slightly faster build time, similar query performance" << std::endl;
    std::cout << "CHD-3.0:      More memory, potentially faster lookups" << std::endl;
    std::cout << "CHD-5.0:      Balanced memory/speed trade-off" << std::endl;
    std::cout << "CHD-7.0:      Memory efficient but slower lookups" << std::endl;
    std::cout << "BBHash-2.0:   Good space usage, supports parallel construction" << std::endl;
    std::cout << "BBHash-2.5:   Faster build, slightly more memory" << std::endl;
    std::cout << "PTHash-98:    Very fast queries (~20-30ns), excellent space efficiency" << std::endl;
    std::cout << "FCH-4.0:      Simple algorithm, educational, good all-around performance" << std::endl;
    std::cout << "FCH-6.0:      Less memory, potentially slower build" << std::endl;
    std::cout << std::endl;
    std::cout << "Recommendations:" << std::endl;
    std::cout << "  - Fastest queries: PTHash-98" << std::endl;
    std::cout << "  - Best space: RecSplit-8" << std::endl;
    std::cout << "  - Parallel build: BBHash (supports multi-threading)" << std::endl;
    std::cout << "  - Educational: FCH (simple, easy to understand)" << std::endl;
    std::cout << "  - General purpose: RecSplit-8 or PTHash-98" << std::endl;
    std::cout << std::endl;

    return 0;
}
