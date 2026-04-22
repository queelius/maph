/**
 * @file bench_partitioned_sweep.cpp
 * @brief Sweeps shard count x thread count for partitioned_phf.
 *
 * Question this answers: is the default of ~15000 keys per shard optimal, or
 * should we go smaller (more shards, each faster to build, more parallelism)
 * or larger (fewer shards, less metadata overhead)?
 *
 * Usage:
 *   bench_partitioned_sweep                         # default: 1M + 10M, shards=16..1024
 *   bench_partitioned_sweep --keys=1000000
 *   bench_partitioned_sweep --keys=10000000 --threads=1,4,8 --shards=32,128,512
 *   bench_partitioned_sweep --distribution=url --keys=1000000
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
    size_t keys;
    std::string distribution;
    size_t shards;
    size_t threads;
    double build_ms;
    double bits_per_key;
    size_t memory_kb;
    double query_median_ns;
    double throughput_mqps;
    bool ok;
};

row run(const std::vector<std::string>& keys,
        const std::string& distribution,
        size_t shards,
        size_t threads,
        size_t total_queries)
{
    using clock = std::chrono::high_resolution_clock;
    using std::chrono::duration_cast;
    using std::chrono::microseconds;

    row r{};
    r.keys = keys.size();
    r.distribution = distribution;
    r.shards = shards;
    r.threads = threads;
    r.ok = false;

    std::cerr << "  shards=" << shards << " T=" << threads << " ..." << std::flush;

    auto t0 = clock::now();
    auto built = partitioned_phf<phobic5>::builder{}
        .add_all(keys)
        .with_shards(shards)
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
    r.memory_kb = built->memory_bytes() / 1024;
    auto qs = measure_queries(*built, keys, total_queries);
    r.query_median_ns = qs.median_ns;
    r.throughput_mqps = qs.throughput_mqps;
    std::cerr << " " << r.build_ms << " ms, " << r.bits_per_key
              << " b/k, " << r.query_median_ns << " ns/q\n";
    return r;
}

void print_header() {
    std::cout << std::left
        << std::setw(11) << "dist"
        << std::right
        << std::setw(10) << "keys"
        << std::setw(8)  << "shards"
        << std::setw(10) << "keys/shd"
        << std::setw(8)  << "threads"
        << std::setw(12) << "build_ms"
        << std::setw(14) << "bits_per_key"
        << std::setw(10) << "mem_kb"
        << std::setw(12) << "query_ns"
        << std::setw(12) << "mqps"
        << std::setw(5)  << "ok"
        << '\n';
}

void print_row(const row& r) {
    size_t ks = r.shards ? r.keys / r.shards : 0;
    std::cout << std::left
        << std::setw(11) << r.distribution
        << std::right << std::fixed
        << std::setw(10) << r.keys
        << std::setw(8)  << r.shards
        << std::setw(10) << ks
        << std::setw(8)  << r.threads
        << std::setw(12) << std::setprecision(2) << r.build_ms
        << std::setw(14) << std::setprecision(3) << r.bits_per_key
        << std::setw(10) << r.memory_kb
        << std::setw(12) << std::setprecision(2) << r.query_median_ns
        << std::setw(12) << std::setprecision(2) << r.throughput_mqps
        << std::setw(5)  << (r.ok ? "1" : "0")
        << '\n';
}

} // namespace

int main(int argc, char** argv) {
    cli_args args(argc, argv);

    // Key counts: --keys=1000000,10000000  (default: both)
    auto key_counts = args.get_size_list("keys", {1'000'000, 10'000'000});

    // Shard counts: --shards=8,16,32,...  (default: 8..1024)
    auto shard_counts = args.get_size_list("shards",
        {8, 16, 32, 64, 128, 256, 512, 1024});

    // Threads: --threads=1,8 (default)
    auto thread_counts = args.get_size_list("threads", {1, 8});

    // Distribution: --distribution=random|sequential|url|variable
    std::string dist = args.get_string("distribution", "random");

    size_t total_queries = args.get_size("queries", 500'000);

    std::cerr << "partitioned<phobic5> shard-count sweep\n"
              << "  distribution: " << dist << "\n"
              << "  keys: ";
    for (auto k : key_counts) std::cerr << k << ' ';
    std::cerr << "\n  shards: ";
    for (auto s : shard_counts) std::cerr << s << ' ';
    std::cerr << "\n  threads: ";
    for (auto t : thread_counts) std::cerr << t << ' ';
    std::cerr << "\n  queries: " << total_queries << "\n\n";

    print_header();

    for (size_t kc : key_counts) {
        std::cerr << "=== " << kc << " keys ===\n";
        std::cerr << "generating " << dist << " keys ..." << std::flush;
        auto keys = gen_keys_by_name(dist, kc);
        std::cerr << " done (" << keys.size() << " unique)\n";

        for (size_t t : thread_counts) {
            for (size_t s : shard_counts) {
                // Skip shards > keys (degenerate).
                if (s > keys.size()) continue;
                auto r = run(keys, dist, s, t, total_queries);
                print_row(r);
                std::cout.flush();
            }
        }
        std::cout << '\n';
    }
    return 0;
}
