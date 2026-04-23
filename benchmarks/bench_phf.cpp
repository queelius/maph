/**
 * @file bench_phf.cpp
 * @brief Core benchmark suite for perfect_hash_function algorithms.
 *
 * Reports for every algorithm at every key-count:
 *   - build time (ms)
 *   - range_size (m) given num_keys (n); alpha = m/n
 *   - bits per key (pure PHF, no fingerprints)
 *   - in-memory bytes
 *   - serialized bytes
 *   - query latency (median, p99 ns/query)
 *   - query throughput (millions of queries per second)
 *
 * All algorithms get the same key set and the same query index sequence.
 * Seeds are fixed for reproducibility.
 *
 * Usage:
 *   bench_phf                    # default: 10000 100000 1000000
 *   bench_phf 1000 10000         # custom key counts
 */

#include "bench_harness.hpp"

#include <maph/algorithms/bbhash.hpp>
#include <maph/algorithms/chd.hpp>
#include <maph/algorithms/fch.hpp>
#include <maph/algorithms/phobic.hpp>
#include <maph/algorithms/pthash.hpp>
#include <maph/algorithms/recsplit.hpp>
#include <maph/algorithms/shock_hash.hpp>

#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using namespace maph;
using namespace maph::bench;

/**
 * Run one algorithm against a key set and collect a full result row.
 *
 * Builder is deduced from the factory lambda; the PHF type comes from the
 * builder's build() return (inside result<PHF>).
 */
template<typename BuilderFactory>
result_row run_algo(const std::string& name,
                    const std::vector<std::string>& keys,
                    BuilderFactory make_builder,
                    size_t total_queries)
{
    using clock = std::chrono::high_resolution_clock;
    using std::chrono::duration_cast;
    using std::chrono::microseconds;

    result_row r{};
    r.algorithm = name;
    r.key_count = keys.size();
    r.ok = false;
    r.fp_rate = 0.0;  // pure PHF has no FP semantics

    reset_peak_rss();
    auto t0 = clock::now();
    auto built = make_builder().add_all(keys).build();
    auto t1 = clock::now();
    r.build_ms = duration_cast<microseconds>(t1 - t0).count() / 1000.0;
    r.build_peak_rss_kb = get_peak_rss_kb();

    if (!built.has_value()) return r;
    auto& phf = built.value();

    r.ok = true;
    r.range_size = phf.range_size();
    r.bits_per_key = phf.bits_per_key();
    r.memory_bytes = phf.memory_bytes();
    r.serialized_bytes = phf.serialize().size();

    auto qs = measure_queries(phf, keys, total_queries);
    r.query_median_ns = qs.median_ns;
    r.query_p99_ns = qs.p99_ns;
    r.query_mqps = qs.throughput_mqps;

    return r;
}

struct algo_entry {
    std::string name;
    std::function<result_row(const std::vector<std::string>&, size_t)> run;
    size_t max_keys;  // skip if key_count exceeds this (e.g. pthash is small-set only)
};

int main(int argc, char** argv) {
    std::vector<size_t> key_counts = {10'000, 100'000, 1'000'000};
    if (argc > 1) {
        key_counts.clear();
        for (int i = 1; i < argc; ++i) key_counts.push_back(std::stoul(argv[i]));
    }

    const size_t total_queries = 1'000'000;

    std::vector<algo_entry> algos = {
        {"phobic5", [&](const auto& keys, size_t q) {
            return run_algo("phobic5", keys, [] { return phobic5::builder{}; }, q);
        }, /*max_keys=*/1'000'000'000},

        {"phobic3", [&](const auto& keys, size_t q) {
            return run_algo("phobic3", keys, [] { return phobic3::builder{}; }, q);
        }, /*max_keys=*/1'000'000'000},

        // phobic7 does not build in minimal mode at any observed scale
        // (pilot search exhausts retries). Kept in the algorithm list for
        // documentation but capped low so the default sweep skips it.
        {"phobic7", [&](const auto& keys, size_t q) {
            return run_algo("phobic7", keys, [] { return phobic7::builder{}; }, q);
        }, /*max_keys=*/1'000},

        {"recsplit8", [&](const auto& keys, size_t q) {
            return run_algo("recsplit8", keys, [] { return recsplit8::builder{}; }, q);
        }, /*max_keys=*/1'000'000'000},

        // recsplit16 works at 10K but fails at 100K+. Cap accordingly.
        {"recsplit16", [&](const auto& keys, size_t q) {
            return run_algo("recsplit16", keys, [] { return recsplit16::builder{}; }, q);
        }, /*max_keys=*/50'000},

        {"chd", [&](const auto& keys, size_t q) {
            return run_algo("chd", keys, [] { return chd_hasher::builder{}; }, q);
        }, /*max_keys=*/1'000'000'000},

        {"bbhash3", [&](const auto& keys, size_t q) {
            return run_algo("bbhash3", keys, [] { return bbhash3::builder{}; }, q);
        }, /*max_keys=*/1'000'000'000},

        {"bbhash5", [&](const auto& keys, size_t q) {
            return run_algo("bbhash5", keys, [] { return bbhash5::builder{}; }, q);
        }, /*max_keys=*/1'000'000'000},

        {"fch", [&](const auto& keys, size_t q) {
            return run_algo("fch", keys, [] { return fch_hasher::builder{}; }, q);
        }, /*max_keys=*/1'000'000'000},

        // PTHash can't build large sets in the current implementation.
        {"pthash98", [&](const auto& keys, size_t q) {
            return run_algo("pthash98", keys, [] { return pthash98::builder{}; }, q);
        }, /*max_keys=*/500},

        // shock_hash: bucketed 2-choice cuckoo with choice bits via ribbon<1>.
        // Non-minimal (range ~1.67 * num_keys at default settings) but
        // achieves ~1.9 b/k structurally, well below PHOBIC's 2.7.
        {"shock_hash64", [&](const auto& keys, size_t q) {
            return run_algo("shock_hash64", keys,
                [] { return shock_hash<64>::builder{}; }, q);
        }, /*max_keys=*/1'000'000},

        {"shock_hash128", [&](const auto& keys, size_t q) {
            return run_algo("shock_hash128", keys,
                [] { return shock_hash<128>::builder{}; }, q);
        }, /*max_keys=*/1'000'000},
    };

    std::cerr << "maph PHF benchmark suite\n"
              << "  key counts: ";
    for (auto kc : key_counts) std::cerr << kc << ' ';
    std::cerr << "\n  queries per measurement: " << total_queries << "\n"
              << "  algorithms: " << algos.size() << "\n\n";

    print_tsv_header(std::cout);

    for (size_t kc : key_counts) {
        std::cerr << "=== " << kc << " keys ===\n";
        auto keys = gen_random_keys(kc);
        if (keys.size() < kc) {
            std::cerr << "  (deduplication reduced to " << keys.size() << ")\n";
        }

        for (auto& algo : algos) {
            if (keys.size() > algo.max_keys) {
                std::cerr << "  skip " << algo.name
                          << " (> " << algo.max_keys << " keys)\n";
                continue;
            }
            std::cerr << "  " << algo.name << " ..." << std::flush;
            auto r = algo.run(keys, total_queries);
            if (r.ok) {
                std::cerr << " " << r.build_ms << "ms build, "
                          << r.bits_per_key << " bits/key, "
                          << r.query_median_ns << " ns/query\n";
            } else {
                std::cerr << " BUILD FAILED\n";
            }
            print_tsv_row(std::cout, r);
            std::cout.flush();
        }
    }

    return 0;
}
