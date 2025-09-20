/**
 * @file bench_ultra_hash.cpp
 * @brief Comprehensive benchmarks for ultra-optimized hash implementation
 *
 * Tests performance across:
 * - Various data sizes (1K to 10M keys)
 * - Different thread counts (1 to max cores)
 * - SIMD configurations (scalar, AVX2, AVX-512)
 * - NUMA configurations
 * - Cache effects and memory bandwidth
 */

#include "maph/perfect_hash_ultra.hpp"
#include "maph/perfect_hash.hpp"
#include "maph/hash_optimized.hpp"
#include "maph/perfect_hash_optimized.hpp"

#include <iostream>
#include <iomanip>
#include <chrono>
#include <random>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <thread>
#include <functional>
#include <map>

#ifdef MAPH_HAS_OPENMP
#include <omp.h>
#endif

#ifdef HAS_NUMA
#include <numa.h>
#endif

using namespace std::chrono;
using namespace maph;

// ===== BENCHMARK FRAMEWORK =====

class BenchmarkFramework {
private:
    struct TestCase {
        std::string name;
        size_t num_keys;
        size_t key_length;
        size_t num_lookups;
    };

    struct Result {
        double construction_ms;
        double lookup_single_ns;
        double lookup_batch_ns;
        double memory_mb;
        double throughput_mops;
        size_t thread_count;
        std::string config;
    };

    std::mt19937_64 rng_{42};
    std::vector<TestCase> test_cases_;
    std::map<std::string, std::vector<Result>> results_;

    // Generate random string
    std::string generate_random_string(size_t len) {
        static const char alphabet[] =
            "abcdefghijklmnopqrstuvwxyz"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "0123456789";

        std::string str;
        str.reserve(len);

        for (size_t i = 0; i < len; ++i) {
            str += alphabet[rng_() % (sizeof(alphabet) - 1)];
        }

        return str;
    }

    // Generate test data
    std::vector<std::string> generate_keys(size_t count, size_t avg_length) {
        std::vector<std::string> keys;
        keys.reserve(count);

        for (size_t i = 0; i < count; ++i) {
            // Vary length around average
            size_t len = avg_length + (rng_() % (avg_length / 2)) - (avg_length / 4);
            len = std::max(size_t(1), len);
            keys.push_back(generate_random_string(len));
        }

        return keys;
    }

    // High-resolution timer
    template<typename F>
    double measure_time_ms(F&& func, size_t warmup = 2, size_t iterations = 5) {
        // Warmup
        for (size_t i = 0; i < warmup; ++i) {
            func();
        }

        // Measure
        std::vector<double> times;
        for (size_t i = 0; i < iterations; ++i) {
            auto start = high_resolution_clock::now();
            func();
            auto end = high_resolution_clock::now();
            times.push_back(duration<double, std::milli>(end - start).count());
        }

        // Return median
        std::sort(times.begin(), times.end());
        return times[times.size() / 2];
    }

public:
    BenchmarkFramework() {
        // Define test cases
        test_cases_ = {
            {"Tiny", 100, 20, 10000},
            {"Small", 1000, 30, 100000},
            {"Medium", 10000, 40, 500000},
            {"Large", 100000, 50, 1000000},
            {"XLarge", 1000000, 60, 5000000},
            {"Huge", 5000000, 70, 10000000},
        };
    }

    // Run benchmark for UltraPerfectHash
    void benchmark_ultra(const TestCase& test) {
        std::cout << "\n=== Testing UltraPerfectHash: " << test.name
                  << " (" << test.num_keys << " keys) ===\n";

        // Generate test data
        auto keys = generate_keys(test.num_keys, test.key_length);
        std::vector<std::string_view> key_views;
        for (const auto& k : keys) {
            key_views.push_back(k);
        }

        // Generate lookup keys (50% hits, 50% misses)
        std::vector<std::string_view> lookup_keys;
        for (size_t i = 0; i < test.num_lookups; ++i) {
            if (rng_() % 2 == 0 && !keys.empty()) {
                lookup_keys.push_back(keys[rng_() % keys.size()]);
            } else {
                lookup_keys.push_back("miss_" + std::to_string(i));
            }
        }

        // Test different thread counts
        std::vector<int> thread_counts = {1, 2, 4, 8, 16, 32};

#ifdef MAPH_HAS_OPENMP
        int max_threads = omp_get_max_threads();
        thread_counts.push_back(max_threads);

        // Remove duplicates and sort
        std::sort(thread_counts.begin(), thread_counts.end());
        thread_counts.erase(std::unique(thread_counts.begin(), thread_counts.end()),
                           thread_counts.end());

        // Filter to available threads
        thread_counts.erase(
            std::remove_if(thread_counts.begin(), thread_counts.end(),
                          [max_threads](int t) { return t > max_threads; }),
            thread_counts.end()
        );
#else
        thread_counts = {1};  // Single-threaded only
#endif

        // Test different configurations
        struct Config {
            std::string name;
            ultra::UltraHashConfig config;
        };

        std::vector<Config> configs = {
            {"Scalar", {.enable_avx2 = false, .enable_avx512 = false, .numa_aware = false}},
            {"AVX2", {.enable_avx2 = true, .enable_avx512 = false, .numa_aware = false}},
        };

        // Add AVX-512 if supported
        if (__builtin_cpu_supports("avx512f")) {
            configs.push_back({"AVX512", {.enable_avx2 = true, .enable_avx512 = true, .numa_aware = false}});
        }

        // Add NUMA if available
#ifdef HAS_NUMA
        if (numa_available() >= 0) {
            configs.push_back({"NUMA+AVX2", {.enable_avx2 = true, .enable_avx512 = false, .numa_aware = true}});
        }
#endif

        // Baseline (single-threaded scalar)
        double baseline_construction = 0;
        double baseline_lookup = 0;

        for (const auto& cfg : configs) {
            for (int threads : thread_counts) {
                Result result;
                result.thread_count = threads;
                result.config = cfg.name;

                // Configure
                auto config = cfg.config;
                config.max_threads = threads;

                // Construction benchmark
                ultra::UltraPerfectHash hash(config);

                result.construction_ms = measure_time_ms([&]() {
                    hash.build(key_views);
                });

                if (threads == 1 && cfg.name == "Scalar") {
                    baseline_construction = result.construction_ms;
                }

                // Memory usage
                result.memory_mb = hash.memory_usage() / (1024.0 * 1024.0);

                // Single lookup benchmark
                auto lookup_time = measure_time_ms([&]() {
                    for (size_t i = 0; i < 10000; ++i) {
                        auto idx = hash.lookup(lookup_keys[i % lookup_keys.size()]);
                        volatile bool found = idx.has_value();
                        (void)found;
                    }
                }, 2, 10);
                result.lookup_single_ns = (lookup_time * 1e6) / 10000;

                if (threads == 1 && cfg.name == "Scalar") {
                    baseline_lookup = result.lookup_single_ns;
                }

                // Batch lookup benchmark
                std::vector<std::optional<uint32_t>> batch_results;
                auto batch_time = measure_time_ms([&]() {
                    hash.lookup_batch(lookup_keys, batch_results);
                });
                result.lookup_batch_ns = (batch_time * 1e6) / lookup_keys.size();

                // Throughput
                result.throughput_mops = (lookup_keys.size() / batch_time) / 1000.0;

                // Store result
                results_[test.name].push_back(result);

                // Print result
                std::cout << std::setw(12) << cfg.name
                         << std::setw(8) << threads << " threads"
                         << std::setw(12) << std::fixed << std::setprecision(2)
                         << result.construction_ms << " ms"
                         << std::setw(12) << std::fixed << std::setprecision(1)
                         << result.lookup_single_ns << " ns"
                         << std::setw(12) << std::fixed << std::setprecision(1)
                         << result.lookup_batch_ns << " ns/op"
                         << std::setw(12) << std::fixed << std::setprecision(2)
                         << result.throughput_mops << " Mops"
                         << std::setw(10) << std::fixed << std::setprecision(2)
                         << result.memory_mb << " MB";

                // Show speedup
                if (baseline_construction > 0) {
                    double speedup = baseline_construction / result.construction_ms;
                    std::cout << std::setw(10) << std::fixed << std::setprecision(2)
                             << speedup << "x";
                }

                std::cout << "\n";
            }
        }
    }

    // Compare with existing implementations
    void benchmark_comparison(const TestCase& test) {
        std::cout << "\n=== Performance Comparison: " << test.name << " ===\n";

        auto keys = generate_keys(test.num_keys, test.key_length);
        std::vector<std::string_view> key_views;
        for (const auto& k : keys) {
            key_views.push_back(k);
        }

        // Benchmark UltraPerfectHash
        {
            ultra::UltraHashConfig config;
            config.enable_avx2 = true;
#ifdef MAPH_HAS_OPENMP
            config.max_threads = omp_get_max_threads();
#endif

            ultra::UltraPerfectHash ultra_hash(config);

            auto construction_time = measure_time_ms([&]() {
                ultra_hash.build(key_views);
            });

            std::cout << std::setw(25) << "UltraPerfectHash:"
                     << std::setw(12) << std::fixed << std::setprecision(2)
                     << construction_time << " ms"
                     << std::setw(12) << std::fixed << std::setprecision(2)
                     << (ultra_hash.memory_usage() / 1024.0 / 1024.0) << " MB"
                     << "\n";
        }

        // Benchmark perfect::SmallPerfectHash
        if (test.num_keys < 1000) {
            perfect::SmallPerfectHash<std::string_view> small_hash;

            auto construction_time = measure_time_ms([&]() {
                small_hash.build(key_views);
            });

            std::cout << std::setw(25) << "SmallPerfectHash:"
                     << std::setw(12) << std::fixed << std::setprecision(2)
                     << construction_time << " ms"
                     << std::setw(12) << std::fixed << std::setprecision(2)
                     << (small_hash.memory_usage() / 1024.0 / 1024.0) << " MB"
                     << "\n";
        }

        // Add more comparisons as needed
    }

    // Run all benchmarks
    void run_all() {
        std::cout << "===================================\n";
        std::cout << "   Ultra Hash Performance Suite    \n";
        std::cout << "===================================\n";

#ifdef MAPH_HAS_OPENMP
        std::cout << "OpenMP: Enabled (" << omp_get_max_threads() << " threads)\n";
#else
        std::cout << "OpenMP: Disabled\n";
#endif

        std::cout << "AVX2: " << (__builtin_cpu_supports("avx2") ? "Yes" : "No") << "\n";
        std::cout << "AVX-512: " << (__builtin_cpu_supports("avx512f") ? "Yes" : "No") << "\n";
#ifdef HAS_NUMA
        std::cout << "NUMA: " << (numa_available() >= 0 ? "Yes" : "No") << "\n";
#else
        std::cout << "NUMA: No\n";
#endif

        for (const auto& test : test_cases_) {
            benchmark_ultra(test);
            benchmark_comparison(test);

            // Don't run huge tests by default
            if (test.num_keys >= 5000000) {
                std::cout << "\n(Skipping larger tests for quick run. "
                         << "Use --full for complete benchmark)\n";
                break;
            }
        }

        print_summary();
    }

    // Print summary statistics
    void print_summary() {
        std::cout << "\n===================================\n";
        std::cout << "         Summary Results           \n";
        std::cout << "===================================\n";

        for (const auto& [test_name, test_results] : results_) {
            std::cout << "\n" << test_name << ":\n";

            // Find best configurations
            auto best_construction = std::min_element(
                test_results.begin(), test_results.end(),
                [](const Result& a, const Result& b) {
                    return a.construction_ms < b.construction_ms;
                }
            );

            auto best_throughput = std::max_element(
                test_results.begin(), test_results.end(),
                [](const Result& a, const Result& b) {
                    return a.throughput_mops < b.throughput_mops;
                }
            );

            if (best_construction != test_results.end()) {
                std::cout << "  Best construction: " << best_construction->config
                         << " (" << best_construction->thread_count << " threads) - "
                         << best_construction->construction_ms << " ms\n";
            }

            if (best_throughput != test_results.end()) {
                std::cout << "  Best throughput: " << best_throughput->config
                         << " (" << best_throughput->thread_count << " threads) - "
                         << best_throughput->throughput_mops << " Mops\n";
            }

            // Calculate parallel efficiency
            auto single_thread = std::find_if(
                test_results.begin(), test_results.end(),
                [](const Result& r) { return r.thread_count == 1; }
            );

            if (single_thread != test_results.end()) {
                for (const auto& r : test_results) {
                    if (r.config == single_thread->config && r.thread_count > 1) {
                        double efficiency = (single_thread->construction_ms / r.construction_ms)
                                          / r.thread_count * 100;
                        std::cout << "  Parallel efficiency (" << r.thread_count
                                 << " threads): " << std::fixed << std::setprecision(1)
                                 << efficiency << "%\n";
                    }
                }
            }
        }
    }
};

// ===== SPECIALIZED BENCHMARKS =====

class SpecializedBenchmarks {
public:
    // Benchmark SIMD hash performance
    static void benchmark_simd_hash() {
        std::cout << "\n=== SIMD Hash Performance ===\n";

        std::vector<size_t> sizes = {100, 1000, 10000, 100000, 1000000};

        for (size_t size : sizes) {
            std::vector<std::string> strings;
            std::vector<std::string_view> views;

            // Generate test strings
            std::mt19937_64 rng(42);
            for (size_t i = 0; i < size; ++i) {
                std::string s = "key_" + std::to_string(i) + "_" + std::to_string(rng());
                strings.push_back(s);
                views.push_back(strings.back());
            }

            std::vector<uint64_t> hashes(size);

            // Scalar
            auto scalar_time = measure([&]() {
                for (size_t i = 0; i < size; ++i) {
                    hashes[i] = ultra::SimdOps::hash_single(views[i], 0);
                }
            });

            // AVX2
            double avx2_time = 0;
            if (__builtin_cpu_supports("avx2")) {
                avx2_time = measure([&]() {
                    ultra::SimdOps::hash_batch_avx2(views.data(), size, hashes.data(), 0);
                });
            }

            // AVX-512
            double avx512_time = 0;
            if (__builtin_cpu_supports("avx512f")) {
                avx512_time = measure([&]() {
                    ultra::SimdOps::hash_batch_avx512(views.data(), size, hashes.data(), 0);
                });
            }

            std::cout << "Size " << std::setw(8) << size << ": "
                     << "Scalar=" << std::fixed << std::setprecision(3)
                     << scalar_time << "ms";

            if (avx2_time > 0) {
                std::cout << ", AVX2=" << avx2_time << "ms ("
                         << (scalar_time / avx2_time) << "x)";
            }

            if (avx512_time > 0) {
                std::cout << ", AVX512=" << avx512_time << "ms ("
                         << (scalar_time / avx512_time) << "x)";
            }

            std::cout << "\n";
        }
    }

    // Benchmark cache effects
    static void benchmark_cache_effects() {
        std::cout << "\n=== Cache Effects Analysis ===\n";

        std::vector<size_t> strides = {1, 4, 8, 16, 32, 64, 128, 256};
        const size_t array_size = 10'000'000;

        std::vector<uint64_t> data(array_size);
        std::mt19937_64 rng(42);
        for (auto& d : data) {
            d = rng();
        }

        for (size_t stride : strides) {
            auto time = measure([&]() {
                uint64_t sum = 0;
                for (size_t i = 0; i < array_size; i += stride) {
                    sum += data[i];
                }
                volatile uint64_t result = sum;
                (void)result;
            });

            double bandwidth_gb_s = (array_size / stride * sizeof(uint64_t))
                                  / (time * 1e6);

            std::cout << "Stride " << std::setw(4) << stride << ": "
                     << std::fixed << std::setprecision(3) << time << " ms, "
                     << std::fixed << std::setprecision(2) << bandwidth_gb_s
                     << " GB/s\n";
        }
    }

private:
    template<typename F>
    static double measure(F&& func) {
        auto start = high_resolution_clock::now();
        func();
        auto end = high_resolution_clock::now();
        return duration<double, std::milli>(end - start).count();
    }
};

// ===== MAIN =====

int main(int argc, char* argv[]) {
    bool run_full = false;
    bool run_specialized = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--full") {
            run_full = true;
        } else if (arg == "--specialized") {
            run_specialized = true;
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                     << "Options:\n"
                     << "  --full         Run full benchmark suite\n"
                     << "  --specialized  Run specialized benchmarks\n"
                     << "  --help         Show this help\n";
            return 0;
        }
    }

    BenchmarkFramework framework;
    framework.run_all();

    if (run_specialized) {
        SpecializedBenchmarks::benchmark_simd_hash();
        SpecializedBenchmarks::benchmark_cache_effects();
    }

    return 0;
}