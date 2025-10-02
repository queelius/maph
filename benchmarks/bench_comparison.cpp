/**
 * @file bench_comparison.cpp
 * @brief Comparison benchmark: maph v3 vs std::unordered_map
 *
 * Fair comparison against in-process hash table to isolate
 * maph's actual performance advantage (not just avoiding network overhead).
 */

#include "benchmark_utils.hpp"
#include "maph/v3/maph.hpp"
#include <iostream>
#include <unordered_map>
#include <string>

using namespace maph::v3;
using namespace maph::bench;

int main(int argc, char* argv[]) {
    std::cout << "=== maph v3 vs std::unordered_map Comparison ===\n\n";

    const size_t num_keys = (argc > 1) ? std::stoull(argv[1]) : 100000;
    const size_t num_queries = (argc > 2) ? std::stoull(argv[2]) : 100000;
    const size_t value_size = 200;

    std::cout << "Configuration:\n";
    std::cout << "  Keys:    " << num_keys << "\n";
    std::cout << "  Queries: " << num_queries << "\n";
    std::cout << "  Value size: " << value_size << " bytes\n\n";

    // Generate test data
    key_generator keys(num_keys);
    value_generator values(value_size);

    // ===== Test 1: maph v3 =====
    std::cout << "=== Testing maph v3 ===\n";

    maph::maph::config cfg{slot_count{num_keys * 2}};
    cfg.enable_cache = false;
    auto maph_db = maph::maph::create_memory(cfg);

    // Populate maph
    std::cout << "Populating maph...\n";
    timer maph_insert_timer;
    for (size_t i = 0; i < num_keys; ++i) {
        const auto& key = keys.get(i);
        auto value = values.generate(key);
        maph_db.set(key, value);
    }
    double maph_insert_time_ms = maph_insert_timer.elapsed_ms();

    // Measure maph GET latency
    std::cout << "Measuring maph GET latency...\n";
    std::vector<double> maph_latencies;
    maph_latencies.reserve(num_queries);

    zipfian_generator zipf1(num_keys, 0.99);
    for (size_t i = 0; i < num_queries; ++i) {
        auto idx = zipf1.next();
        const auto& key = keys.get(idx);

        timer t;
        [[maybe_unused]] auto result = maph_db.get(key);
        maph_latencies.push_back(static_cast<double>(t.elapsed_ns()));
    }

    auto maph_stats = compute_stats(maph_latencies);

    // ===== Test 2: std::unordered_map =====
    std::cout << "\n=== Testing std::unordered_map ===\n";

    std::unordered_map<std::string, std::string> std_map;
    std_map.reserve(num_keys);

    // Populate std::unordered_map
    std::cout << "Populating std::unordered_map...\n";
    timer std_insert_timer;
    for (size_t i = 0; i < num_keys; ++i) {
        const auto& key = keys.get(i);
        auto value = values.generate(key);
        std_map[key] = std::move(value);
    }
    double std_insert_time_ms = std_insert_timer.elapsed_ms();

    // Measure std::unordered_map GET latency
    std::cout << "Measuring std::unordered_map GET latency...\n";
    std::vector<double> std_latencies;
    std_latencies.reserve(num_queries);

    zipfian_generator zipf2(num_keys, 0.99);
    for (size_t i = 0; i < num_queries; ++i) {
        auto idx = zipf2.next();
        const auto& key = keys.get(idx);

        timer t;
        [[maybe_unused]] auto it = std_map.find(key);
        std_latencies.push_back(static_cast<double>(t.elapsed_ns()));
    }

    auto std_stats = compute_stats(std_latencies);

    // ===== Results =====
    std::cout << "\n=== Results ===\n\n";

    maph_stats.print("maph v3 GET Latency", "ns");
    std::cout << "\n";
    std_stats.print("std::unordered_map GET Latency", "ns");

    // Comparison table
    std::cout << "\n=== Comparison Table (CSV) ===\n";
    std::cout << "System,Min,Median,p90,p99,p99.9,p99.99\n";
    maph_stats.print_csv("maph");
    std_stats.print_csv("std::unordered_map");

    // Speedup analysis
    std::cout << "\n=== Speedup Analysis ===\n";
    double median_speedup = std_stats.median / maph_stats.median;
    double p99_speedup = std_stats.p99 / maph_stats.p99;

    std::cout << "maph is " << std::fixed << std::setprecision(2)
              << median_speedup << "x faster (median)\n";
    std::cout << "maph is " << p99_speedup << "x faster (p99)\n";

    // Insert performance
    std::cout << "\n=== Insert Performance ===\n";
    std::cout << "maph insert time:         " << std::fixed << std::setprecision(2)
              << maph_insert_time_ms << " ms\n";
    std::cout << "std::unordered_map insert time: " << std_insert_time_ms << " ms\n";
    double insert_speedup = std_insert_time_ms / maph_insert_time_ms;
    std::cout << "Speedup: " << insert_speedup << "x\n";

    // Memory usage (approximate)
    std::cout << "\n=== Memory Usage ===\n";
    size_t maph_memory = num_keys * 2 * 512;  // slots * slot_size
    size_t std_memory_estimate = num_keys * (sizeof(std::string) * 2 + value_size + 100);  // rough estimate

    std::cout << "maph memory:         ~" << (maph_memory / 1024 / 1024) << " MB\n";
    std::cout << "std::unordered_map memory: ~" << (std_memory_estimate / 1024 / 1024) << " MB (estimate)\n";

    return 0;
}
