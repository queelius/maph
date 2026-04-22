/**
 * @file bench_phobic_parallel.cpp
 * @brief Measures PHOBIC parallel pilot-search speedup.
 *
 * For each configuration (bucket_size, key_count), times the build with
 * threads=1, 2, 4, 8 and reports the speedup relative to threads=1.
 * Query latency is measured too so we can verify the parallel version
 * produces an equivalent PHF.
 *
 * Usage:
 *   bench_phobic_parallel                        # default: 100K 1M
 *   bench_phobic_parallel 100000 250000 1000000  # custom
 */

#include "bench_harness.hpp"

#include <maph/algorithms/phobic.hpp>

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
    std::string algo;
    size_t keys;
    size_t threads;
    double build_ms;
    double bits_per_key;
    double query_median_ns;
    double speedup;   // relative to threads=1 in the same (algo, keys) group
    bool ok;
};

template<typename PHF>
row run_one(const std::string& algo,
            const std::vector<std::string>& keys,
            size_t threads,
            size_t total_queries) {
    using clock = std::chrono::high_resolution_clock;
    using std::chrono::duration_cast;
    using std::chrono::microseconds;

    row r{};
    r.algo = algo;
    r.keys = keys.size();
    r.threads = threads;
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

void print_header() {
    std::cout << std::left
        << std::setw(10) << "algo"
        << std::right
        << std::setw(10) << "keys"
        << std::setw(8)  << "threads"
        << std::setw(14) << "build_ms"
        << std::setw(14) << "bits_per_key"
        << std::setw(14) << "query_ns"
        << std::setw(10) << "speedup"
        << std::setw(5)  << "ok"
        << '\n';
}

void print_row(const row& r) {
    std::cout << std::left
        << std::setw(10) << r.algo
        << std::right << std::fixed
        << std::setw(10) << r.keys
        << std::setw(8)  << r.threads
        << std::setw(14) << std::setprecision(2) << r.build_ms
        << std::setw(14) << std::setprecision(3) << r.bits_per_key
        << std::setw(14) << std::setprecision(2) << r.query_median_ns
        << std::setw(10) << std::setprecision(2) << r.speedup
        << std::setw(5)  << (r.ok ? "1" : "0")
        << '\n';
}

template<typename PHF>
void sweep(const std::string& algo, size_t kc, size_t total_queries) {
    auto keys = gen_random_keys(kc);
    std::vector<row> rows;
    for (size_t t : {size_t{1}, size_t{2}, size_t{4}, size_t{8}}) {
        std::cerr << "  " << algo << " keys=" << kc << " threads=" << t << " ..." << std::flush;
        auto r = run_one<PHF>(algo, keys, t, total_queries);
        std::cerr << (r.ok ? " ok" : " FAILED") << " (" << r.build_ms << " ms)\n";
        rows.push_back(r);
    }
    double base = rows.empty() ? 0.0 : rows[0].build_ms;
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

    const size_t total_queries = 250'000;  // small; this bench is about build not query

    std::cerr << "PHOBIC parallel build speedup\n"
              << "  key counts: ";
    for (auto k : key_counts) std::cerr << k << ' ';
    std::cerr << "\n  threads: 1, 2, 4, 8\n\n";

    print_header();

    for (size_t kc : key_counts) {
        sweep<phobic3>("phobic3", kc, total_queries);
        sweep<phobic4>("phobic4", kc, total_queries);
        sweep<phobic5>("phobic5", kc, total_queries);
    }
    return 0;
}
