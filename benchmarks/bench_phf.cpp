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

#include <maph/algorithms/phobic.hpp>
#include <maph/algorithms/recsplit.hpp>
#include <maph/algorithms/chd.hpp>
#include <maph/algorithms/bbhash.hpp>
#include <maph/algorithms/fch.hpp>
#include <maph/algorithms/pthash.hpp>

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

    auto t0 = clock::now();
    auto built = make_builder().add_all(keys).build();
    auto t1 = clock::now();
    r.build_ms = duration_cast<microseconds>(t1 - t0).count() / 1000.0;

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

        {"phobic7", [&](const auto& keys, size_t q) {
            return run_algo("phobic7", keys, [] { return phobic7::builder{}; }, q);
        }, /*max_keys=*/1'000'000'000},

        {"recsplit8", [&](const auto& keys, size_t q) {
            return run_algo("recsplit8", keys, [] { return recsplit8::builder{}; }, q);
        }, /*max_keys=*/1'000'000'000},

        {"recsplit16", [&](const auto& keys, size_t q) {
            return run_algo("recsplit16", keys, [] { return recsplit16::builder{}; }, q);
        }, /*max_keys=*/1'000'000'000},

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
