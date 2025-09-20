/**
 * @file test_simple_openmp.cpp
 * @brief Test for simplified OpenMP perfect hash implementation
 */

#include "maph/perfect_hash_simple_openmp.hpp"
#include <iostream>
#include <chrono>
#include <vector>
#include <string>
#include <random>
#include <iomanip>

using namespace std::chrono;
using namespace maph::simple_openmp;

// Generate test data
std::vector<std::string_view> generate_test_data(
    std::vector<std::string>& storage,
    size_t count
) {
    std::vector<std::string_view> views;
    std::mt19937_64 rng(42);

    storage.clear();
    storage.reserve(count);
    views.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        storage.push_back("key_" + std::to_string(i) + "_" + std::to_string(rng()));
        views.push_back(storage.back());
    }

    return views;
}

// Measure time with warmup
template<typename F>
double measure_ms(F&& func, size_t warmup = 2, size_t iterations = 5) {
    // Warmup
    for (size_t i = 0; i < warmup; ++i) {
        func();
    }

    // Measure
    double total = 0;
    for (size_t i = 0; i < iterations; ++i) {
        auto start = high_resolution_clock::now();
        func();
        auto end = high_resolution_clock::now();
        total += duration<double, std::milli>(end - start).count();
    }

    return total / iterations;
}

int main() {
    std::cout << "=== OpenMP Perfect Hash Performance Test ===\n\n";

    // Check features
    std::cout << "System Configuration:\n";
#ifdef MAPH_HAS_OPENMP
    std::cout << "  OpenMP: Yes (" << omp_get_max_threads() << " threads)\n";
#else
    std::cout << "  OpenMP: No\n";
#endif
    std::cout << "  AVX2: " << (__builtin_cpu_supports("avx2") ? "Yes" : "No") << "\n\n";

    // Test different sizes
    std::vector<size_t> sizes = {100, 1000, 10000, 100000, 500000};

    for (size_t size : sizes) {
        std::cout << "Testing with " << size << " keys:\n";

        std::vector<std::string> storage;
        auto keys = generate_test_data(storage, size);

        // Test configurations
        struct TestCase {
            std::string name;
            Config config;
        };

        std::vector<TestCase> tests = {
            {"Single-threaded", {.max_threads = 1, .enable_avx2 = false, .enable_parallel = false}},
        };

        if (__builtin_cpu_supports("avx2")) {
            tests.push_back({"AVX2", {.max_threads = 1, .enable_avx2 = true, .enable_parallel = false}});
        }

#ifdef MAPH_HAS_OPENMP
        tests.push_back({"Parallel(2)", {.max_threads = 2, .enable_avx2 = false, .enable_parallel = true}});
        tests.push_back({"Parallel(4)", {.max_threads = 4, .enable_avx2 = false, .enable_parallel = true}});
        tests.push_back({"Parallel(max)", {.max_threads = 0, .enable_avx2 = false, .enable_parallel = true}});

        if (__builtin_cpu_supports("avx2")) {
            tests.push_back({"AVX2+Parallel", {.max_threads = 0, .enable_avx2 = true, .enable_parallel = true}});
        }
#endif

        double baseline_construction = 0;
        double baseline_lookup = 0;

        for (const auto& test : tests) {
            SimplePerfectHash hash(test.config);

            // Construction
            double construction_ms = measure_ms([&]() {
                hash.build(keys);
            });

            if (test.name == "Single-threaded") {
                baseline_construction = construction_ms;
            }

            // Single lookups
            double lookup_ms = measure_ms([&]() {
                size_t found = 0;
                for (size_t i = 0; i < std::min(size_t(1000), size); ++i) {
                    auto result = hash.lookup(keys[i]);
                    if (result.has_value()) found++;
                }
            });
            double ns_per_lookup = (lookup_ms * 1e6) / std::min(size_t(1000), size);

            if (test.name == "Single-threaded") {
                baseline_lookup = ns_per_lookup;
            }

            // Batch lookups
            std::vector<std::optional<uint32_t>> results;
            double batch_ms = measure_ms([&]() {
                hash.lookup_batch(keys, results);
            });
            double batch_ns_per_op = (batch_ms * 1e6) / keys.size();

            // Print results
            std::cout << "  " << std::setw(20) << std::left << test.name
                     << "Construction: " << std::setw(8) << std::fixed << std::setprecision(2)
                     << construction_ms << " ms";

            if (baseline_construction > 0) {
                double speedup = baseline_construction / construction_ms;
                std::cout << " (" << std::setprecision(1) << speedup << "x)";
            }

            std::cout << ", Lookup: " << std::setw(8) << std::setprecision(1)
                     << ns_per_lookup << " ns";

            if (baseline_lookup > 0) {
                double speedup = baseline_lookup / ns_per_lookup;
                std::cout << " (" << std::setprecision(1) << speedup << "x)";
            }

            std::cout << ", Batch: " << std::setw(8) << std::setprecision(1)
                     << batch_ns_per_op << " ns/op";

            std::cout << "\n";
        }

        std::cout << "\n";
    }

    // Direct SIMD comparison
    std::cout << "=== Direct Hash Function Comparison ===\n";
    std::vector<std::string> storage;
    auto keys = generate_test_data(storage, 100000);
    std::vector<uint64_t> hashes;

    // Scalar
    auto scalar_time = measure_ms([&]() {
        hashes.clear();
        hashes.resize(keys.size());
        for (size_t i = 0; i < keys.size(); ++i) {
            hashes[i] = hash_fnv1a(keys[i], 42);
        }
    });
    std::cout << "Scalar: " << scalar_time << " ms\n";

#ifdef __AVX2__
    if (__builtin_cpu_supports("avx2")) {
        auto avx2_time = measure_ms([&]() {
            hash_batch_avx2(keys, hashes, 42);
        });
        std::cout << "AVX2: " << avx2_time << " ms (speedup: "
                 << (scalar_time / avx2_time) << "x)\n";
    }
#endif

#ifdef MAPH_HAS_OPENMP
    Config parallel_config;
    parallel_config.enable_parallel = true;
    parallel_config.max_threads = 0;

    auto parallel_time = measure_ms([&]() {
        hash_batch_parallel(keys, hashes, 42, parallel_config);
    });
    std::cout << "Parallel: " << parallel_time << " ms (speedup: "
             << (scalar_time / parallel_time) << "x)\n";
#endif

    std::cout << "\nTest completed successfully!\n";
    return 0;
}