/**
 * @file bench_partitioned_algos.cpp
 * @brief Compares different inner PHF algorithms under partitioned_phf.
 *
 * Partitioning made phobic5 at 1M keys 60x faster. Does the same trick help
 * other PHF algorithms? Which inner algorithm gives the best build time,
 * bits/key, or query speed when partitioned?
 *
 * Usage:
 *   bench_partitioned_algos                         # 1M keys, 8 threads
 *   bench_partitioned_algos --keys=1000000 --threads=8 --distribution=url
 */

#include "bench_harness.hpp"

#include <maph/algorithms/phobic.hpp>
#include <maph/algorithms/recsplit.hpp>
#include <maph/algorithms/chd.hpp>
#include <maph/algorithms/bbhash.hpp>
#include <maph/algorithms/fch.hpp>
#include <maph/composition/partitioned.hpp>

#include <chrono>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace maph;
using namespace maph::bench;

namespace {

struct row {
    std::string config;
    size_t keys;
    size_t threads;
    double build_ms;
    double bits_per_key;
    double query_median_ns;
    double throughput_mqps;
    bool ok;
};

template<typename Inner>
row run_partitioned(const std::string& name,
                    const std::vector<std::string>& keys,
                    size_t threads,
                    size_t total_queries) {
    using clock = std::chrono::high_resolution_clock;
    using std::chrono::duration_cast;
    using std::chrono::microseconds;

    row r{};
    r.config = name;
    r.keys = keys.size();
    r.threads = threads;
    r.ok = false;

    std::cerr << "  " << name << " T=" << threads << " ..." << std::flush;

    auto t0 = clock::now();
    auto built = typename partitioned_phf<Inner>::builder{}
        .add_all(keys)
        .with_threads(threads)
        .build();
    auto t1 = clock::now();
    r.build_ms = duration_cast<microseconds>(t1 - t0).count() / 1000.0;
    if (!built.has_value()) {
        std::cerr << " BUILD FAILED\n";
        return r;
    }
    r.ok = true;
    r.bits_per_key = built->bits_per_key();
    auto qs = measure_queries(*built, keys, total_queries);
    r.query_median_ns = qs.median_ns;
    r.throughput_mqps = qs.throughput_mqps;

    std::cerr << " " << r.build_ms << " ms, " << r.bits_per_key
              << " b/k, " << r.query_median_ns << " ns/q\n";
    return r;
}

template<typename PHF>
row run_plain(const std::string& name,
              const std::vector<std::string>& keys,
              size_t threads,
              size_t total_queries,
              bool supports_threads = true) {
    using clock = std::chrono::high_resolution_clock;
    using std::chrono::duration_cast;
    using std::chrono::microseconds;

    row r{};
    r.config = name;
    r.keys = keys.size();
    r.threads = threads;
    r.ok = false;

    std::cerr << "  " << name << " ..." << std::flush;

    auto t0 = clock::now();
    auto builder = typename PHF::builder{};
    builder.add_all(keys);
    // Only PHOBIC supports with_threads; everything else is single-threaded.
    if constexpr (requires { builder.with_threads(threads); }) {
        if (supports_threads) builder.with_threads(threads);
    }
    auto built = builder.build();
    auto t1 = clock::now();
    r.build_ms = duration_cast<microseconds>(t1 - t0).count() / 1000.0;
    if (!built.has_value()) {
        std::cerr << " BUILD FAILED\n";
        return r;
    }
    r.ok = true;
    r.bits_per_key = built->bits_per_key();
    auto qs = measure_queries(*built, keys, total_queries);
    r.query_median_ns = qs.median_ns;
    r.throughput_mqps = qs.throughput_mqps;

    std::cerr << " " << r.build_ms << " ms, " << r.bits_per_key
              << " b/k, " << r.query_median_ns << " ns/q\n";
    return r;
}

void print_header() {
    std::cout << std::left
        << std::setw(30) << "config"
        << std::right
        << std::setw(10) << "keys"
        << std::setw(8)  << "threads"
        << std::setw(12) << "build_ms"
        << std::setw(14) << "bits_per_key"
        << std::setw(12) << "query_ns"
        << std::setw(12) << "mqps"
        << std::setw(5)  << "ok"
        << '\n';
}

void print_row(const row& r) {
    std::cout << std::left
        << std::setw(30) << r.config
        << std::right << std::fixed
        << std::setw(10) << r.keys
        << std::setw(8)  << r.threads
        << std::setw(12) << std::setprecision(2) << r.build_ms
        << std::setw(14) << std::setprecision(3) << r.bits_per_key
        << std::setw(12) << std::setprecision(2) << r.query_median_ns
        << std::setw(12) << std::setprecision(2) << r.throughput_mqps
        << std::setw(5)  << (r.ok ? "1" : "0")
        << '\n';
}

} // namespace

int main(int argc, char** argv) {
    cli_args args(argc, argv);

    size_t keys_count = args.get_size("keys", 1'000'000);
    size_t threads = args.get_size("threads", 8);
    std::string dist = args.get_string("distribution", "random");
    size_t total_queries = args.get_size("queries", 500'000);

    std::cerr << "partitioned across inner PHF algorithms\n"
              << "  keys: " << keys_count << " (" << dist << ")\n"
              << "  threads: " << threads << "\n"
              << "  queries: " << total_queries << "\n\n";

    auto keys = gen_keys_by_name(dist, keys_count);
    std::cerr << "generated " << keys.size() << " unique keys\n\n";

    print_header();

    // Partitioned variants.
    print_row(run_partitioned<phobic5>("partitioned<phobic5>", keys, threads, total_queries));
    print_row(run_partitioned<phobic4>("partitioned<phobic4>", keys, threads, total_queries));
    print_row(run_partitioned<phobic3>("partitioned<phobic3>", keys, threads, total_queries));
    print_row(run_partitioned<bbhash5>("partitioned<bbhash5>", keys, threads, total_queries));
    print_row(run_partitioned<bbhash3>("partitioned<bbhash3>", keys, threads, total_queries));
    print_row(run_partitioned<recsplit8>("partitioned<recsplit8>", keys, threads, total_queries));
    print_row(run_partitioned<chd_hasher>("partitioned<chd>", keys, threads, total_queries));
    print_row(run_partitioned<fch_hasher>("partitioned<fch>", keys, threads, total_queries));

    std::cout << '\n';

    // Plain (single PHF) for comparison. phobic5 uses fat-bucket parallelism;
    // others are single-threaded (and will be slow at 1M).
    print_row(run_plain<phobic5>("phobic5 (fat+bucket)", keys, threads, total_queries));
    print_row(run_plain<bbhash5>("bbhash5 (serial)", keys, threads, total_queries, false));
    print_row(run_plain<recsplit8>("recsplit8 (serial)", keys, threads, total_queries, false));

    return 0;
}
