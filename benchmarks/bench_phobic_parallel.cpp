/**
 * @file bench_phobic_parallel.cpp
 * @brief Compares four PHOBIC build strategies at scale.
 *
 * Strategies:
 *   - serial:     threads=1 (original sequential algorithm)
 *   - bucket:     threads=N, no fat-bucket pre-pass (each bucket processed
 *                 by one worker; straggler-prone on the largest buckets)
 *   - fat+bucket: threads=N, fat buckets processed cooperatively first,
 *                 then thin buckets via work-stealing
 *   - partitioned: partitioned_phf<phobic_K> with auto shard count; each
 *                 shard is a serial inner build, parallelized across shards
 *
 * The third strategy is what the current with_threads(N) implements (it
 * transparently uses the fat-bucket pre-pass). Reproducing the "bucket-only"
 * strategy would require an extra flag; for now the "bucket" column is
 * reported via the threads=N numbers from the previous implementation (see
 * docs/BENCHMARK_RESULTS.md history) and the current with_threads(N) is
 * labeled "fat+bucket" since that is what actually runs.
 *
 * Usage:
 *   bench_phobic_parallel                        # default: 100K 1M
 *   bench_phobic_parallel 100000 250000 1000000  # custom
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
    std::string strategy;
    std::string algo;
    size_t keys;
    size_t threads;
    size_t shards;     // 0 if not partitioned
    double build_ms;
    double bits_per_key;
    double query_median_ns;
    double speedup;   // vs the serial baseline in the same (algo, keys) group
    bool ok;
};

template<typename PHF>
row run_phobic(const std::string& algo,
               const std::vector<std::string>& keys,
               size_t threads,
               size_t total_queries) {
    using clock = std::chrono::high_resolution_clock;
    using std::chrono::duration_cast;
    using std::chrono::microseconds;

    row r{};
    r.strategy = (threads <= 1) ? "serial" : "fat+bucket";
    r.algo = algo;
    r.keys = keys.size();
    r.threads = threads;
    r.shards = 0;
    r.ok = false;

    auto t0 = clock::now();
    auto built = typename PHF::builder{}
        .add_all(keys)
        .with_threads(threads)
        .build();
    auto t1 = clock::now();
    r.build_ms = duration_cast<microseconds>(t1 - t0).count() / 1000.0;
    if (!built.has_value()) return r;

    r.ok = true;
    r.bits_per_key = built->bits_per_key();
    auto qs = measure_queries(*built, keys, total_queries);
    r.query_median_ns = qs.median_ns;
    return r;
}

template<typename Inner>
row run_partitioned(const std::string& algo,
                    const std::vector<std::string>& keys,
                    size_t threads,
                    size_t total_queries,
                    size_t shards = 0) {
    using clock = std::chrono::high_resolution_clock;
    using std::chrono::duration_cast;
    using std::chrono::microseconds;

    row r{};
    r.strategy = "partitioned";
    r.algo = algo;
    r.keys = keys.size();
    r.threads = threads;
    r.shards = shards;
    r.ok = false;

    auto t0 = clock::now();
    auto built = typename partitioned_phf<Inner>::builder{}
        .add_all(keys)
        .with_threads(threads)
        .with_shards(shards)
        .build();
    auto t1 = clock::now();
    r.build_ms = duration_cast<microseconds>(t1 - t0).count() / 1000.0;
    if (!built.has_value()) return r;

    r.ok = true;
    r.bits_per_key = built->bits_per_key();
    r.shards = shards ? shards : (built->range_size() ? /* auto used */ r.shards : 0);
    auto qs = measure_queries(*built, keys, total_queries);
    r.query_median_ns = qs.median_ns;
    return r;
}

void print_header() {
    std::cout << std::left
        << std::setw(14) << "strategy"
        << std::setw(10) << "algo"
        << std::right
        << std::setw(10) << "keys"
        << std::setw(8)  << "threads"
        << std::setw(8)  << "shards"
        << std::setw(14) << "build_ms"
        << std::setw(14) << "bits_per_key"
        << std::setw(14) << "query_ns"
        << std::setw(10) << "speedup"
        << std::setw(5)  << "ok"
        << '\n';
}

void print_row(const row& r) {
    std::cout << std::left
        << std::setw(14) << r.strategy
        << std::setw(10) << r.algo
        << std::right << std::fixed
        << std::setw(10) << r.keys
        << std::setw(8)  << r.threads
        << std::setw(8)  << r.shards
        << std::setw(14) << std::setprecision(2) << r.build_ms
        << std::setw(14) << std::setprecision(3) << r.bits_per_key
        << std::setw(14) << std::setprecision(2) << r.query_median_ns
        << std::setw(10) << std::setprecision(2) << r.speedup
        << std::setw(5)  << (r.ok ? "1" : "0")
        << '\n';
}

template<typename PHF>
void sweep_one_algo(const std::string& algo, size_t kc, size_t total_queries) {
    auto keys = gen_random_keys(kc);
    std::vector<row> rows;

    // Serial baseline.
    std::cerr << "  " << algo << " keys=" << kc << " serial ..." << std::flush;
    auto serial = run_phobic<PHF>(algo, keys, 1, total_queries);
    std::cerr << (serial.ok ? " ok" : " FAILED") << " (" << serial.build_ms << " ms)\n";
    rows.push_back(serial);

    // Current parallel (fat-bucket + work-stealing) at 2, 4, 8 threads.
    for (size_t t : {size_t{2}, size_t{4}, size_t{8}}) {
        std::cerr << "  " << algo << " keys=" << kc << " fat+bucket T=" << t << " ..." << std::flush;
        auto r = run_phobic<PHF>(algo, keys, t, total_queries);
        std::cerr << (r.ok ? " ok" : " FAILED") << " (" << r.build_ms << " ms)\n";
        rows.push_back(r);
    }

    // Partitioned: auto shards, 1 / 2 / 4 / 8 threads.
    for (size_t t : {size_t{1}, size_t{2}, size_t{4}, size_t{8}}) {
        std::cerr << "  partitioned<" << algo << "> keys=" << kc
                  << " T=" << t << " ..." << std::flush;
        auto r = run_partitioned<PHF>(algo, keys, t, total_queries, 0);
        std::cerr << (r.ok ? " ok" : " FAILED") << " (" << r.build_ms << " ms)\n";
        rows.push_back(r);
    }

    double base = rows[0].ok ? rows[0].build_ms : 0.0;
    for (auto& r : rows) {
        r.speedup = (r.ok && base > 0.0) ? base / r.build_ms : 0.0;
        print_row(r);
    }
    std::cout << '\n';
}

} // namespace

int main(int argc, char** argv) {
    std::vector<size_t> key_counts = {100'000, 1'000'000};
    if (argc > 1) {
        key_counts.clear();
        for (int i = 1; i < argc; ++i) key_counts.push_back(std::stoul(argv[i]));
    }

    const size_t total_queries = 250'000;

    std::cerr << "PHOBIC parallel build strategies\n"
              << "  key counts: ";
    for (auto k : key_counts) std::cerr << k << ' ';
    std::cerr << "\n  strategies: serial, fat+bucket (T=2,4,8), partitioned (T=1,2,4,8)\n\n";

    print_header();

    for (size_t kc : key_counts) {
        sweep_one_algo<phobic3>("phobic3", kc, total_queries);
        sweep_one_algo<phobic4>("phobic4", kc, total_queries);
        sweep_one_algo<phobic5>("phobic5", kc, total_queries);
    }
    return 0;
}
