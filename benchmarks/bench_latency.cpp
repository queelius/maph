/**
 * @file bench_latency.cpp
 * @brief Single-threaded latency benchmark for maph v3
 *
 * Measures latency distribution for GET operations to validate
 * claims in the paper about sub-microsecond performance.
 */

#include "benchmark_utils.hpp"
#include "maph/v3/maph.hpp"
#include <iostream>
#include <memory>

using namespace maph::v3;
using namespace maph::bench;

int main(int argc, char* argv[]) {
    std::cout << "=== maph v3 Single-Threaded Latency Benchmark ===\n\n";

    // Configuration
    const size_t num_keys = (argc > 1) ? std::stoull(argv[1]) : 1000000;
    const size_t num_queries = (argc > 2) ? std::stoull(argv[2]) : 1000000;
    const size_t value_size = 200;  // Average JSON document size

    std::cout << "Configuration:\n";
    std::cout << "  Keys:      " << num_keys << "\n";
    std::cout << "  Queries:   " << num_queries << "\n";
    std::cout << "  Value size: " << value_size << " bytes\n\n";

    // Create database
    std::cout << "Creating in-memory database...\n";
    maph::maph::config cfg{slot_count{num_keys * 3}};  // Lower load factor to avoid fills
    cfg.enable_cache = false;
    cfg.max_probes = 20;  // Increase max probes
    auto db = maph::maph::create_memory(cfg);

    // Generate keys and values
    std::cout << "Generating test data...\n";
    key_generator keys(num_keys);
    value_generator values(value_size);

    // Populate database
    std::cout << "Populating database...\n";
    timer populate_timer;
    for (size_t i = 0; i < num_keys; ++i) {
        const auto& key = keys.get(i);
        auto value = values.generate(key);
        auto result = db.set(key, value);
        if (!result) {
            std::cerr << "Failed to insert key " << i << "\n";
            return 1;
        }

        if ((i + 1) % 100000 == 0) {
            std::cout << "  Inserted " << (i + 1) << " keys...\n";
        }
    }
    std::cout << "Population complete in " << populate_timer.elapsed_ms() << " ms\n";
    std::cout << "Load factor: " << db.load_factor() << "\n\n";

    // Benchmark 1: Random GET operations
    std::cout << "=== Benchmark 1: Random GET Operations ===\n";
    std::vector<double> get_latencies;
    get_latencies.reserve(num_queries);

    std::mt19937_64 rng(42);
    std::uniform_int_distribution<size_t> dist(0, num_keys - 1);

    // Warmup
    std::cout << "Warming up...\n";
    for (size_t i = 0; i < 10000; ++i) {
        auto idx = dist(rng);
        [[maybe_unused]] auto result = db.get(keys.get(idx));
    }

    // Measure
    std::cout << "Measuring " << num_queries << " GET operations...\n";
    for (size_t i = 0; i < num_queries; ++i) {
        auto idx = dist(rng);
        const auto& key = keys.get(idx);

        timer t;
        auto result = db.get(key);
        get_latencies.push_back(static_cast<double>(t.elapsed_ns()));

        if (!result) {
            std::cerr << "Key not found: " << key << "\n";
        }

        if ((i + 1) % 100000 == 0) {
            std::cout << "  Completed " << (i + 1) << " queries...\n";
        }
    }

    auto get_stats = compute_stats(get_latencies);
    get_stats.print("Random GET Latency", "ns");

    // Benchmark 2: Sequential GET operations
    std::cout << "\n=== Benchmark 2: Sequential GET Operations ===\n";
    std::vector<double> seq_get_latencies;
    seq_get_latencies.reserve(std::min(num_queries, num_keys));

    std::cout << "Measuring sequential GET operations...\n";
    for (size_t i = 0; i < std::min(num_queries, num_keys); ++i) {
        const auto& key = keys.get(i);

        timer t;
        auto result = db.get(key);
        seq_get_latencies.push_back(static_cast<double>(t.elapsed_ns()));

        if (!result) {
            std::cerr << "Key not found: " << key << "\n";
        }
    }

    auto seq_stats = compute_stats(seq_get_latencies);
    seq_stats.print("Sequential GET Latency", "ns");

    // Benchmark 3: Negative lookups (keys that don't exist)
    std::cout << "\n=== Benchmark 3: Negative Lookups ===\n";
    std::vector<double> negative_latencies;
    negative_latencies.reserve(10000);

    std::cout << "Measuring negative lookups...\n";
    for (size_t i = 0; i < 10000; ++i) {
        std::string missing_key = "missing:" + std::to_string(i);

        timer t;
        auto result = db.get(missing_key);
        negative_latencies.push_back(static_cast<double>(t.elapsed_ns()));

        if (result) {
            std::cerr << "Unexpected: found missing key\n";
        }
    }

    auto neg_stats = compute_stats(negative_latencies);
    neg_stats.print("Negative Lookup Latency", "ns");

    // Print summary table for paper
    std::cout << "\n=== Summary Table (CSV format for paper) ===\n";
    std::cout << "Operation,Min,Median,p90,p99,p99.9,p99.99\n";
    get_stats.print_csv("Random GET");
    seq_stats.print_csv("Sequential GET");
    neg_stats.print_csv("Negative Lookup");

    // Calculate throughput
    double total_time_s = populate_timer.elapsed_ns() / 1e9;
    double throughput = num_queries / (get_stats.mean / 1e9) / 1e6;  // Million ops/sec

    std::cout << "\n=== Performance Summary ===\n";
    std::cout << "Single-threaded throughput: " << std::fixed << std::setprecision(2)
              << throughput << " million ops/sec\n";
    std::cout << "Average latency: " << std::fixed << std::setprecision(1)
              << get_stats.mean << " ns\n";
    std::cout << "Median latency: " << get_stats.median << " ns\n";

    return 0;
}
