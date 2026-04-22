/**
 * @file bench_harness.hpp
 * @brief Measurement utilities for PHF benchmarks.
 *
 * Key design choices:
 *
 * - **Batched query timing**: per-query chrono::now() adds 20-30ns of overhead,
 *   which is comparable to fast query latencies. Instead we time N queries
 *   in a tight loop and divide; clock overhead amortizes to near-zero.
 *
 * - **Sub-batch percentiles**: individual queries are too fast to time
 *   meaningfully (sub-nanosecond signal, ns-resolution clock). Instead we
 *   time M batches of B queries each, producing M "average per-query"
 *   samples whose distribution we summarize.
 *
 * - **Volatile sink**: prevents DCE of slot_for() results without requiring
 *   Google benchmark's DoNotOptimize or inline asm.
 */

#pragma once

#include <maph/core.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace maph::bench {

// ===== VALUE SINK =====
//
// Force the compiler to treat the argument as observed. Prevents it from
// optimizing away slot_for() calls whose result would otherwise be unused.
inline volatile uint64_t sink = 0;

inline void consume(slot_index s) noexcept { sink ^= s.value; }
inline void consume(bool b) noexcept { sink ^= b ? 1 : 0; }

// ===== KEY GENERATION =====
//
// 16-byte random binary keys. Deterministic for a given seed. Deduplicated.
inline std::vector<std::string> gen_random_keys(size_t count, uint64_t seed = 42) {
    std::vector<std::string> keys;
    keys.reserve(count);
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<int> byte_dist(0, 255);
    for (size_t i = 0; i < count; ++i) {
        std::string key(16, '\0');
        for (auto& c : key) c = static_cast<char>(byte_dist(rng));
        keys.push_back(std::move(key));
    }
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    return keys;
}

// ===== QUERY TIMING =====

struct query_stats {
    double median_ns;     // ns per query, median of sub-batch averages
    double p99_ns;        // ns per query, 99th percentile of sub-batch averages
    double throughput_mqps;  // millions of queries per second, from single batch
};

/**
 * Measure query throughput and per-query latency percentiles.
 *
 * total_queries = M * B. Each of M sub-batches times B queries, producing
 * M samples. Throughput is derived from total elapsed time over total_queries.
 */
template<typename PHF>
query_stats measure_queries(
    const PHF& phf,
    const std::vector<std::string>& keys,
    size_t total_queries = 1'000'000,
    size_t sub_batch_size = 1000,
    uint64_t seed = 12345)
{
    using clock = std::chrono::high_resolution_clock;
    using std::chrono::duration_cast;
    using std::chrono::nanoseconds;

    // Precompute a random sequence of key indices.
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<size_t> kd(0, keys.size() - 1);
    std::vector<size_t> indices(total_queries);
    for (auto& i : indices) i = kd(rng);

    // Warm up: caches, TLB, branch predictors. Discarded.
    for (size_t i = 0; i < 10000 && i < total_queries; ++i) {
        consume(phf.slot_for(keys[indices[i]]));
    }

    // Sub-batch timing for percentiles.
    const size_t M = total_queries / sub_batch_size;
    std::vector<double> batch_ns_per_query;
    batch_ns_per_query.reserve(M);

    auto global_start = clock::now();
    for (size_t m = 0; m < M; ++m) {
        size_t base = m * sub_batch_size;
        auto t0 = clock::now();
        for (size_t b = 0; b < sub_batch_size; ++b) {
            consume(phf.slot_for(keys[indices[base + b]]));
        }
        auto t1 = clock::now();
        double batch_ns = static_cast<double>(duration_cast<nanoseconds>(t1 - t0).count());
        batch_ns_per_query.push_back(batch_ns / static_cast<double>(sub_batch_size));
    }
    auto global_end = clock::now();

    std::sort(batch_ns_per_query.begin(), batch_ns_per_query.end());
    double median = batch_ns_per_query[M / 2];
    double p99 = batch_ns_per_query[static_cast<size_t>(M * 0.99)];

    double total_ns = static_cast<double>(
        duration_cast<nanoseconds>(global_end - global_start).count());
    double mqps = (static_cast<double>(M * sub_batch_size) / total_ns) * 1000.0;

    return {median, p99, mqps};
}

// ===== BUILD TIMING =====

template<typename BuildFn>
double measure_build_ms(BuildFn&& fn) {
    using clock = std::chrono::high_resolution_clock;
    using std::chrono::duration_cast;
    using std::chrono::microseconds;

    auto t0 = clock::now();
    fn();
    auto t1 = clock::now();
    return duration_cast<microseconds>(t1 - t0).count() / 1000.0;
}

// ===== RESULT ROW + PRINTERS =====

struct result_row {
    std::string algorithm;
    size_t key_count;
    size_t range_size;
    double build_ms;
    double bits_per_key;
    size_t memory_bytes;
    size_t serialized_bytes;
    double query_median_ns;
    double query_p99_ns;
    double query_mqps;
    bool ok;
};

inline void print_tsv_header(std::ostream& os) {
    os << "algorithm\tkeys\trange\tbuild_ms\tbits_per_key\tmem_bytes\t"
          "ser_bytes\tquery_med_ns\tquery_p99_ns\tthroughput_mqps\tok\n";
}

inline void print_tsv_row(std::ostream& os, const result_row& r) {
    os << std::fixed
       << r.algorithm << '\t'
       << r.key_count << '\t'
       << r.range_size << '\t'
       << std::setprecision(2) << r.build_ms << '\t'
       << std::setprecision(3) << r.bits_per_key << '\t'
       << r.memory_bytes << '\t'
       << r.serialized_bytes << '\t'
       << std::setprecision(2) << r.query_median_ns << '\t'
       << r.query_p99_ns << '\t'
       << r.query_mqps << '\t'
       << (r.ok ? "1" : "0") << '\n';
}

} // namespace maph::bench
