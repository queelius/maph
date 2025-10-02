/**
 * @file bench_throughput.cpp
 * @brief Multi-threaded throughput benchmark for maph v3
 *
 * Measures throughput scaling with increasing thread counts to validate
 * claims about linear scalability and 98M ops/sec with 16 threads.
 */

#include "benchmark_utils.hpp"
#include "maph/v3/maph.hpp"
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <barrier>

using namespace maph::v3;
using namespace maph::bench;

// Shared state for multi-threaded benchmarking
struct thread_stats {
    std::atomic<uint64_t> operations{0};
    std::atomic<uint64_t> total_latency_ns{0};
    std::atomic<uint64_t> errors{0};
};

void worker_thread(
    const maph::maph& db,
    const key_generator& keys,
    size_t ops_per_thread,
    thread_stats& stats,
    std::barrier<>& start_barrier)
{
    // Wait for all threads to be ready
    start_barrier.arrive_and_wait();

    std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, keys.count() - 1);
    uint64_t local_ops = 0;
    uint64_t local_latency = 0;

    for (size_t i = 0; i < ops_per_thread; ++i) {
        auto idx = dist(rng);
        const auto& key = keys.get(idx);

        timer t;
        auto result = db.get(key);
        auto latency = t.elapsed_ns();

        local_latency += latency;
        local_ops++;

        if (!result) {
            stats.errors.fetch_add(1, std::memory_order_relaxed);
        }
    }

    stats.operations.fetch_add(local_ops, std::memory_order_relaxed);
    stats.total_latency_ns.fetch_add(local_latency, std::memory_order_relaxed);
}

stats run_throughput_test(
    const maph::maph& db,
    const key_generator& keys,
    size_t num_threads,
    size_t ops_per_thread)
{
    std::cout << "  Testing with " << num_threads << " threads...\n";

    thread_stats ts;
    std::barrier start_barrier(num_threads + 1);  // +1 for main thread

    // Launch worker threads
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    timer total_timer;

    for (size_t i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker_thread,
            std::ref(db),
            std::ref(keys),
            ops_per_thread,
            std::ref(ts),
            std::ref(start_barrier));
    }

    // Start all threads simultaneously
    start_barrier.arrive_and_wait();

    // Wait for completion
    for (auto& t : threads) {
        t.join();
    }

    double elapsed_s = total_timer.elapsed_ns() / 1e9;

    // Compute stats
    double throughput_mops = (ts.operations.load() / elapsed_s) / 1e6;
    double avg_latency_ns = static_cast<double>(ts.total_latency_ns.load()) / ts.operations.load();

    std::cout << "    Throughput: " << std::fixed << std::setprecision(2)
              << throughput_mops << " M ops/sec\n";
    std::cout << "    Avg latency: " << std::fixed << std::setprecision(1)
              << avg_latency_ns << " ns\n";
    std::cout << "    Errors: " << ts.errors.load() << "\n";

    return {
        0.0,  // min
        0.0,  // max
        throughput_mops,  // mean (repurposed for throughput)
        avg_latency_ns,   // median (repurposed for avg latency)
        0.0, 0.0, 0.0, 0.0, 0.0,  // percentiles (not measured in throughput test)
        0.0,  // stddev
        ts.operations.load()  // count
    };
}

int main(int argc, char* argv[]) {
    std::cout << "=== maph v3 Multi-Threaded Throughput Benchmark ===\n\n";

    // Configuration
    const size_t num_keys = (argc > 1) ? std::stoull(argv[1]) : 1000000;
    const size_t ops_per_thread = (argc > 2) ? std::stoull(argv[2]) : 1000000;
    const size_t value_size = 200;

    std::cout << "Configuration:\n";
    std::cout << "  Keys:             " << num_keys << "\n";
    std::cout << "  Ops per thread:   " << ops_per_thread << "\n";
    std::cout << "  Value size:       " << value_size << " bytes\n";
    std::cout << "  Hardware threads: " << std::thread::hardware_concurrency() << "\n\n";

    // Create database
    std::cout << "Creating in-memory database...\n";
    maph::maph::config cfg{slot_count{num_keys * 3}};
    cfg.enable_cache = false;
    cfg.max_probes = 20;
    auto db = maph::maph::create_memory(cfg);

    // Generate and populate
    std::cout << "Generating test data...\n";
    key_generator keys(num_keys);
    value_generator values(value_size);

    std::cout << "Populating database...\n";
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

    std::cout << "Load factor: " << db.load_factor() << "\n\n";

    // Run throughput tests with varying thread counts
    std::cout << "=== Throughput Scaling Test ===\n";

    std::vector<size_t> thread_counts = {1, 2, 4, 8};
    auto max_threads = std::thread::hardware_concurrency();
    if (max_threads >= 16) thread_counts.push_back(16);
    if (max_threads >= 32) thread_counts.push_back(32);

    std::vector<std::pair<size_t, stats>> results;

    for (auto num_threads : thread_counts) {
        if (num_threads > max_threads) continue;

        auto s = run_throughput_test(db, keys, num_threads, ops_per_thread);
        results.emplace_back(num_threads, s);
        std::cout << "\n";
    }

    // Print summary table for paper
    std::cout << "=== Throughput Scaling Summary (for paper) ===\n";
    std::cout << "Threads,Throughput (M ops/sec),Avg Latency (ns),Speedup\n";

    double baseline_throughput = results[0].second.mean;  // 1-thread throughput

    for (const auto& [threads, s] : results) {
        double speedup = s.mean / baseline_throughput;
        std::cout << threads << ","
                  << std::fixed << std::setprecision(2) << s.mean << ","
                  << std::fixed << std::setprecision(1) << s.median << ","
                  << std::fixed << std::setprecision(2) << speedup << "\n";
    }

    // Check if we achieved paper claims
    std::cout << "\n=== Comparison with Paper Claims ===\n";
    std::cout << "Paper claims:\n";
    std::cout << "  - 10M ops/sec single-threaded\n";
    std::cout << "  - 98M ops/sec with 16 threads\n\n";

    std::cout << "Actual results:\n";
    std::cout << "  - " << std::fixed << std::setprecision(1)
              << results[0].second.mean << "M ops/sec single-threaded\n";

    // Find 16-thread result if available
    auto it16 = std::find_if(results.begin(), results.end(),
        [](const auto& p) { return p.first == 16; });
    if (it16 != results.end()) {
        std::cout << "  - " << it16->second.mean << "M ops/sec with 16 threads\n";
    }

    return 0;
}
