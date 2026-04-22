/**
 * @file bench_scale.cpp
 * @brief Tests PHOBIC scaling at large key counts.
 *
 * At 10M+ keys, serial phobic5 takes >15 minutes per run, so this benchmark
 * focuses on partitioned_phf (the fast path) and a single fat+bucket
 * datapoint for reference. Use bench_phobic_parallel for full strategy
 * comparisons at 10K-1M scales.
 *
 * Usage:
 *   bench_scale                        # default: 1M 10M
 *   bench_scale 5000000 10000000       # custom
 */

#include "bench_harness.hpp"

#include <maph/algorithms/phobic.hpp>
#include <maph/composition/partitioned.hpp>

#include <chrono>
#include <cstdlib>
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
    double build_s;
    double bits_per_key;
    size_t memory_kb;
    double query_median_ns;
    double throughput_mqps;
    bool ok;
};

template<typename PHF, typename BuilderFn>
row run(const std::string& config,
        const std::vector<std::string>& keys,
        size_t threads,
        BuilderFn make_builder,
        size_t total_queries) {
    using clock = std::chrono::high_resolution_clock;
    using std::chrono::duration_cast;
    using std::chrono::microseconds;

    row r{};
    r.config = config;
    r.keys = keys.size();
    r.threads = threads;
    r.ok = false;

    std::cerr << "  " << config << " T=" << threads
              << " n=" << keys.size() << " ..." << std::flush;

    auto t0 = clock::now();
    auto built = make_builder();
    auto t1 = clock::now();
    r.build_s = duration_cast<microseconds>(t1 - t0).count() / 1'000'000.0;

    if (!built.has_value()) {
        std::cerr << " BUILD FAILED (" << r.build_s << " s)\n";
        return r;
    }
    r.ok = true;
    r.bits_per_key = built->bits_per_key();
    r.memory_kb = built->memory_bytes() / 1024;

    auto qs = measure_queries(*built, keys, total_queries);
    r.query_median_ns = qs.median_ns;
    r.throughput_mqps = qs.throughput_mqps;

    std::cerr << " built " << r.build_s << "s, "
              << r.bits_per_key << " b/k, "
              << r.query_median_ns << " ns/q\n";
    return r;
}

void print_header() {
    std::cout << std::left
        << std::setw(30) << "config"
        << std::right
        << std::setw(12) << "keys"
        << std::setw(8)  << "threads"
        << std::setw(12) << "build_s"
        << std::setw(14) << "bits_per_key"
        << std::setw(12) << "mem_mb"
        << std::setw(12) << "query_ns"
        << std::setw(12) << "mqps"
        << std::setw(5)  << "ok"
        << '\n';
}

void print_row(const row& r) {
    std::cout << std::left
        << std::setw(30) << r.config
        << std::right << std::fixed
        << std::setw(12) << r.keys
        << std::setw(8)  << r.threads
        << std::setw(12) << std::setprecision(3) << r.build_s
        << std::setw(14) << std::setprecision(3) << r.bits_per_key
        << std::setw(12) << std::setprecision(2) << (r.memory_kb / 1024.0)
        << std::setw(12) << std::setprecision(2) << r.query_median_ns
        << std::setw(12) << std::setprecision(2) << r.throughput_mqps
        << std::setw(5)  << (r.ok ? "1" : "0")
        << '\n';
}

} // namespace

int main(int argc, char** argv) {
    std::vector<size_t> key_counts = {1'000'000, 10'000'000};
    if (argc > 1) {
        key_counts.clear();
        for (int i = 1; i < argc; ++i) key_counts.push_back(std::stoul(argv[i]));
    }

    const size_t total_queries = 500'000;  // smaller: query cost is well-characterized from bench_phf

    std::cerr << "Large-scale partitioned PHOBIC benchmark\n"
              << "  key counts: ";
    for (auto k : key_counts) std::cerr << k << ' ';
    std::cerr << "\n\n";

    print_header();

    for (size_t kc : key_counts) {
        std::cerr << "=== " << kc << " keys ===\n";
        std::cerr << "generating keys ..." << std::flush;
        auto keys = gen_random_keys(kc);
        std::cerr << " done (" << keys.size() << " unique)\n";

        // Partitioned across thread counts.
        for (size_t t : {size_t{1}, size_t{2}, size_t{4}, size_t{8}}) {
            auto r = run<partitioned_phf<phobic3>>(
                "partitioned<phobic3>", keys, t,
                [&] {
                    return partitioned_phf<phobic3>::builder{}
                        .add_all(keys).with_threads(t).build();
                },
                total_queries);
            print_row(r);
            std::cout.flush();
        }
        for (size_t t : {size_t{1}, size_t{2}, size_t{4}, size_t{8}}) {
            auto r = run<partitioned_phf<phobic4>>(
                "partitioned<phobic4>", keys, t,
                [&] {
                    return partitioned_phf<phobic4>::builder{}
                        .add_all(keys).with_threads(t).build();
                },
                total_queries);
            print_row(r);
            std::cout.flush();
        }
        for (size_t t : {size_t{1}, size_t{2}, size_t{4}, size_t{8}}) {
            auto r = run<partitioned_phf<phobic5>>(
                "partitioned<phobic5>", keys, t,
                [&] {
                    return partitioned_phf<phobic5>::builder{}
                        .add_all(keys).with_threads(t).build();
                },
                total_queries);
            print_row(r);
            std::cout.flush();
        }

        // One fat+bucket reference for comparison. At 10M, phobic5 serial
        // would take ~17 minutes; fat+bucket at 8T takes ~4-5 minutes.
        // Skip fat+bucket at >= 5M for phobic5.
        if (kc < 5'000'000) {
            auto r = run<phobic5>(
                "fat+bucket<phobic5> T=8", keys, 8,
                [&] {
                    return phobic5::builder{}.add_all(keys).with_threads(8).build();
                },
                total_queries);
            print_row(r);
            std::cout.flush();
        }

        std::cout << '\n';
    }
    return 0;
}
