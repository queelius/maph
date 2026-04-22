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
#include <cstdio>
#include <fstream>
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
// Multiple distributions are supported. All are deterministic given the seed.
// Caller dedups via std::sort + std::unique; random_bytes at 16-byte width
// is effectively collision-free for up to ~10^9 keys so dedup cost is cheap.

// 16-byte random binary keys (default).
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

// Decimal integers as strings: "0", "1", "2", ..., "N-1".
// Tests hash quality for low-entropy, highly-structured input.
inline std::vector<std::string> gen_sequential_keys(size_t count, uint64_t /*seed*/ = 42) {
    std::vector<std::string> keys;
    keys.reserve(count);
    for (size_t i = 0; i < count; ++i) keys.push_back(std::to_string(i));
    return keys;
}

// URL-shaped keys with a fixed prefix + random suffix. Mimics the key shape
// of real URL caches: long, correlated prefix, random distinguishing tail.
inline std::vector<std::string> gen_url_keys(size_t count, uint64_t seed = 42) {
    static constexpr const char* prefix =
        "https://example.com/v1/resource/";
    std::vector<std::string> keys;
    keys.reserve(count);
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<int> hex_dist(0, 15);
    for (size_t i = 0; i < count; ++i) {
        std::string key = prefix;
        for (int j = 0; j < 32; ++j) {
            int v = hex_dist(rng);
            key.push_back(v < 10 ? char('0' + v) : char('a' + v - 10));
        }
        keys.push_back(std::move(key));
    }
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    return keys;
}

// Random binary keys with variable length in [min_len, max_len].
// Probes sensitivity of hash cost to key length.
inline std::vector<std::string> gen_variable_length_keys(
    size_t count, size_t min_len = 4, size_t max_len = 64, uint64_t seed = 42)
{
    std::vector<std::string> keys;
    keys.reserve(count);
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<int> byte_dist(0, 255);
    std::uniform_int_distribution<size_t> len_dist(min_len, max_len);
    for (size_t i = 0; i < count; ++i) {
        size_t len = len_dist(rng);
        std::string key(len, '\0');
        for (auto& c : key) c = static_cast<char>(byte_dist(rng));
        keys.push_back(std::move(key));
    }
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    return keys;
}

// Dispatch by name. Useful for CLI: "--distribution=url".
inline std::vector<std::string>
gen_keys_by_name(const std::string& name, size_t count, uint64_t seed = 42) {
    if (name == "random") return gen_random_keys(count, seed);
    if (name == "sequential") return gen_sequential_keys(count, seed);
    if (name == "url") return gen_url_keys(count, seed);
    if (name == "variable") return gen_variable_length_keys(count, 4, 64, seed);
    // Fall back to the default rather than throw; benchmarks are forgiving.
    return gen_random_keys(count, seed);
}

// ===== PEAK RSS (Linux) =====
//
// Reads VmHWM (peak resident set size) from /proc/self/status. Returns 0 on
// non-Linux or on read failure (benchmarks gracefully omit the column).
//
// Call reset_peak_rss() before a build to zero the high-water mark (via
// /proc/self/clear_refs "5"), then get_peak_rss_kb() after the build to get
// the build's peak memory. Works on Linux 2.6.22+ kernels.
inline size_t get_peak_rss_kb() {
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line)) {
        if (line.compare(0, 6, "VmHWM:") == 0) {
            size_t kb = 0;
            // Format: "VmHWM:\t  <number> kB"
            for (char c : line) if (c >= '0' && c <= '9') kb = kb * 10 + (c - '0');
            return kb;
        }
    }
    return 0;
}

inline void reset_peak_rss() {
    // Write "5" to clear_refs to reset VmHWM. See proc(5) / kernel docs.
    std::ofstream f("/proc/self/clear_refs");
    if (f) f << "5\n";
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
    size_t build_peak_rss_kb;  // 0 if unavailable
    double bits_per_key;
    size_t memory_bytes;
    size_t serialized_bytes;
    double query_median_ns;
    double query_p99_ns;
    double query_mqps;
    double fp_rate;  // empirical false-positive rate; 0 for pure PHF, NaN if not measured
    bool ok;
};

inline void print_tsv_header(std::ostream& os) {
    os << "algorithm\tkeys\trange\tbuild_ms\tbuild_peak_kb\tbits_per_key\t"
          "mem_bytes\tser_bytes\tquery_med_ns\tquery_p99_ns\tthroughput_mqps\t"
          "fp_rate\tok\n";
}

inline void print_tsv_row(std::ostream& os, const result_row& r) {
    os << std::fixed
       << r.algorithm << '\t'
       << r.key_count << '\t'
       << r.range_size << '\t'
       << std::setprecision(2) << r.build_ms << '\t'
       << r.build_peak_rss_kb << '\t'
       << std::setprecision(3) << r.bits_per_key << '\t'
       << r.memory_bytes << '\t'
       << r.serialized_bytes << '\t'
       << std::setprecision(2) << r.query_median_ns << '\t'
       << r.query_p99_ns << '\t'
       << r.query_mqps << '\t'
       << std::setprecision(10) << r.fp_rate << '\t'
       << (r.ok ? "1" : "0") << '\n';
}

// ===== CLI ARGUMENT PARSING =====
//
// Tiny `--key=value` parser. Positional args (no leading `--`) are collected
// separately. No external dependency, no validation beyond what's needed.
//
// Example:
//   cli_args a(argc, argv);
//   size_t n = a.get_size("keys", 1'000'000);
//   size_t t = a.get_size("threads", 8);
//   auto dist = a.get_string("distribution", "random");
//   for (const auto& p : a.positional()) { ... }

class cli_args {
    std::vector<std::string> positional_;
    std::vector<std::pair<std::string, std::string>> named_;

public:
    cli_args(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg.size() >= 2 && arg[0] == '-' && arg[1] == '-') {
                // --key or --key=value
                auto eq = arg.find('=');
                if (eq == std::string::npos) {
                    named_.emplace_back(arg.substr(2), "");
                } else {
                    named_.emplace_back(arg.substr(2, eq - 2), arg.substr(eq + 1));
                }
            } else {
                positional_.push_back(std::move(arg));
            }
        }
    }

    const std::vector<std::string>& positional() const { return positional_; }

    bool has(const std::string& key) const {
        for (const auto& [k, _] : named_) if (k == key) return true;
        return false;
    }

    std::string get_string(const std::string& key, const std::string& fallback) const {
        for (const auto& [k, v] : named_) if (k == key) return v;
        return fallback;
    }

    size_t get_size(const std::string& key, size_t fallback) const {
        auto s = get_string(key, "");
        if (s.empty()) return fallback;
        try { return std::stoul(s); } catch (...) { return fallback; }
    }

    double get_double(const std::string& key, double fallback) const {
        auto s = get_string(key, "");
        if (s.empty()) return fallback;
        try { return std::stod(s); } catch (...) { return fallback; }
    }

    // Parse "1,2,4,8" into {1,2,4,8}. Empty string -> empty vector.
    std::vector<size_t> get_size_list(const std::string& key,
                                       std::vector<size_t> fallback) const {
        auto s = get_string(key, "");
        if (s.empty()) return fallback;
        std::vector<size_t> out;
        size_t start = 0;
        while (start < s.size()) {
            auto end = s.find(',', start);
            if (end == std::string::npos) end = s.size();
            try { out.push_back(std::stoul(s.substr(start, end - start))); }
            catch (...) { /* skip */ }
            start = end + 1;
        }
        return out;
    }
};

} // namespace maph::bench
