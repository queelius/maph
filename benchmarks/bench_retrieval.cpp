/**
 * @file bench_retrieval.cpp
 * @brief Retrieval-grid benchmark: methods x value widths x key counts.
 *
 * Answers the question: given a key set S and a value function v: S -> {0,1}^m,
 * which retrieval method minimizes bits/key at what build-time and query cost?
 *
 * Sweeps:
 *   - methods: ribbon_retrieval<M>, phf_value_array<phobic5, M>,
 *              phf_value_array<bbhash5, M>
 *   - value widths M in {1, 8, 16, 32, 64}
 *   - key counts (from --keys argument; default 100000, 1000000)
 *
 * Usage:
 *   bench_retrieval                                   # default grid
 *   bench_retrieval --keys=100000,1000000             # custom scales
 *   bench_retrieval --keys=10000 --queries=1000000    # small, heavy queries
 *   bench_retrieval --distribution=url                # distribution sensitivity
 */

#include "bench_harness.hpp"

#include <maph/algorithms/bbhash.hpp>
#include <maph/algorithms/phobic.hpp>
#include <maph/composition/partitioned.hpp>
#include <maph/retrieval/phf_value_array.hpp>
#include <maph/retrieval/ribbon_retrieval.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace maph;
using namespace maph::bench;

namespace {

struct row {
    std::string method;
    size_t value_bits;
    size_t keys;
    double build_ms;
    double bits_per_key;
    size_t memory_kb;
    double query_median_ns;
    double throughput_mqps;
    bool ok;
};

// Deterministic value derived from key bytes; truncated to M bits at the
// storage site by the builder. Use 64 bits here and let each benchmark
// truncate as needed.
uint64_t deterministic_value(std::string_view key) noexcept {
    uint64_t h = 0x9e3779b97f4a7c15ULL;
    for (char c : key) {
        h ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        h *= 0xbf58476d1ce4e5b9ULL;
        h ^= h >> 31;
    }
    return h;
}

struct query_stats {
    double median_ns;
    double throughput_mqps;
};

// Measure lookup latency over in-set keys. Value sink prevents DCE.
template <typename Retrieval>
query_stats measure_lookups(const Retrieval& r,
                            const std::vector<std::string>& keys,
                            size_t total_queries) {
    using clock = std::chrono::high_resolution_clock;
    using std::chrono::duration_cast;
    using std::chrono::nanoseconds;

    static constexpr size_t inner_batch = 1000;
    size_t outer = total_queries / inner_batch;
    if (outer == 0) outer = 1;

    std::vector<double> per_batch_ns;
    per_batch_ns.reserve(outer);

    uint64_t sink = 0;
    auto t_total0 = clock::now();
    size_t idx = 0;
    for (size_t i = 0; i < outer; ++i) {
        auto t0 = clock::now();
        for (size_t j = 0; j < inner_batch; ++j) {
            sink ^= static_cast<uint64_t>(r.lookup(keys[idx]));
            idx++;
            if (idx >= keys.size()) idx = 0;
        }
        auto t1 = clock::now();
        double ns = static_cast<double>(duration_cast<nanoseconds>(t1 - t0).count())
                  / static_cast<double>(inner_batch);
        per_batch_ns.push_back(ns);
    }
    auto t_total1 = clock::now();
    consume(sink != 0);

    std::sort(per_batch_ns.begin(), per_batch_ns.end());
    double median = per_batch_ns[per_batch_ns.size() / 2];

    double total_s = static_cast<double>(
        duration_cast<nanoseconds>(t_total1 - t_total0).count()) * 1e-9;
    double total_q = static_cast<double>(outer * inner_batch);
    double mqps = total_q / total_s / 1e6;

    return {median, mqps};
}

// ===== Per-method runners =====

template <unsigned M>
row run_ribbon(const std::vector<std::string>& keys, size_t total_queries) {
    using clock = std::chrono::high_resolution_clock;
    using std::chrono::duration_cast;
    using std::chrono::microseconds;

    row r{};
    r.method = "ribbon<" + std::to_string(M) + ">";
    r.value_bits = M;
    r.keys = keys.size();

    std::cerr << "  " << r.method << " ..." << std::flush;

    auto t0 = clock::now();
    auto built = typename ribbon_retrieval<M>::builder{}
        .add_all_with(std::span<const std::string>{keys},
                      [](std::string_view k) { return deterministic_value(k); })
        .build();
    auto t1 = clock::now();
    r.build_ms = static_cast<double>(duration_cast<microseconds>(t1 - t0).count()) / 1000.0;
    if (!built.has_value()) {
        std::cerr << " BUILD FAILED\n";
        r.ok = false;
        return r;
    }
    r.ok = true;
    r.bits_per_key = built->bits_per_key();
    r.memory_kb = built->memory_bytes() / 1024;
    auto qs = measure_lookups(*built, keys, total_queries);
    r.query_median_ns = qs.median_ns;
    r.throughput_mqps = qs.throughput_mqps;

    std::cerr << " " << r.build_ms << " ms, "
              << r.bits_per_key << " b/k, "
              << r.query_median_ns << " ns/q\n";
    return r;
}

template <typename PHF, unsigned M>
row run_phf_array(const std::string& phf_name,
                  const std::vector<std::string>& keys,
                  size_t total_queries,
                  size_t threads) {
    using clock = std::chrono::high_resolution_clock;
    using std::chrono::duration_cast;
    using std::chrono::microseconds;

    row r{};
    r.method = "pva<" + phf_name + "," + std::to_string(M) + ">";
    r.value_bits = M;
    r.keys = keys.size();

    std::cerr << "  " << r.method << " ..." << std::flush;

    auto t0 = clock::now();
    auto builder = typename phf_value_array<PHF, M>::builder{};
    builder.add_all_with(std::span<const std::string>{keys},
                         [](std::string_view k) { return deterministic_value(k); });
    if constexpr (requires { builder.with_threads(threads); }) {
        builder.with_threads(threads);
    }
    auto built = builder.build();
    auto t1 = clock::now();
    r.build_ms = static_cast<double>(duration_cast<microseconds>(t1 - t0).count()) / 1000.0;
    if (!built.has_value()) {
        std::cerr << " BUILD FAILED\n";
        r.ok = false;
        return r;
    }
    r.ok = true;
    r.bits_per_key = built->bits_per_key();
    r.memory_kb = built->memory_bytes() / 1024;
    auto qs = measure_lookups(*built, keys, total_queries);
    r.query_median_ns = qs.median_ns;
    r.throughput_mqps = qs.throughput_mqps;

    std::cerr << " " << r.build_ms << " ms, "
              << r.bits_per_key << " b/k, "
              << r.query_median_ns << " ns/q\n";
    return r;
}

void print_header() {
    std::cout << std::left
        << std::setw(28) << "method"
        << std::right
        << std::setw(10) << "value_m"
        << std::setw(10) << "keys"
        << std::setw(12) << "build_ms"
        << std::setw(14) << "bits_per_key"
        << std::setw(10) << "mem_kb"
        << std::setw(12) << "query_ns"
        << std::setw(12) << "mqps"
        << std::setw(5)  << "ok"
        << '\n';
}

void print_row(const row& r) {
    std::cout << std::left
        << std::setw(28) << r.method
        << std::right << std::fixed
        << std::setw(10) << r.value_bits
        << std::setw(10) << r.keys
        << std::setw(12) << std::setprecision(2) << r.build_ms
        << std::setw(14) << std::setprecision(3) << r.bits_per_key
        << std::setw(10) << r.memory_kb
        << std::setw(12) << std::setprecision(2) << r.query_median_ns
        << std::setw(12) << std::setprecision(2) << r.throughput_mqps
        << std::setw(5)  << (r.ok ? "1" : "0")
        << '\n';
    std::cout.flush();
}

} // namespace

int main(int argc, char** argv) {
    cli_args args(argc, argv);

    auto key_counts = args.get_size_list("keys", {100'000, 1'000'000});
    std::string dist = args.get_string("distribution", "random");
    size_t total_queries = args.get_size("queries", 500'000);
    size_t threads = args.get_size("threads", 8);

    std::cerr << "bench_retrieval: value-width sweep\n"
              << "  distribution: " << dist << "\n"
              << "  keys:";
    for (auto k : key_counts) std::cerr << ' ' << k;
    std::cerr << "\n  threads (pva only): " << threads << "\n"
              << "  queries per config: " << total_queries << "\n\n";

    print_header();

    for (size_t kc : key_counts) {
        std::cerr << "=== " << kc << " keys (" << dist << ") ===\n";
        std::cerr << "generating " << dist << " keys ..." << std::flush;
        auto keys = gen_keys_by_name(dist, kc);
        std::cerr << " done (" << keys.size() << " unique)\n";

        print_row(run_ribbon<1>(keys, total_queries));
        print_row(run_ribbon<8>(keys, total_queries));
        print_row(run_ribbon<16>(keys, total_queries));
        print_row(run_ribbon<32>(keys, total_queries));
        print_row(run_ribbon<64>(keys, total_queries));

        std::cout << '\n';

        print_row(run_phf_array<phobic5, 1>("phobic5", keys, total_queries, threads));
        print_row(run_phf_array<phobic5, 8>("phobic5", keys, total_queries, threads));
        print_row(run_phf_array<phobic5, 16>("phobic5", keys, total_queries, threads));
        print_row(run_phf_array<phobic5, 32>("phobic5", keys, total_queries, threads));
        print_row(run_phf_array<phobic5, 64>("phobic5", keys, total_queries, threads));

        std::cout << '\n';

        print_row(run_phf_array<bbhash5, 1>("bbhash5", keys, total_queries, threads));
        print_row(run_phf_array<bbhash5, 8>("bbhash5", keys, total_queries, threads));
        print_row(run_phf_array<bbhash5, 16>("bbhash5", keys, total_queries, threads));
        print_row(run_phf_array<bbhash5, 32>("bbhash5", keys, total_queries, threads));
        print_row(run_phf_array<bbhash5, 64>("bbhash5", keys, total_queries, threads));

        std::cout << '\n';

        // Partitioned PHOBIC as PHF. Keeps PHOBIC's near-optimal space while
        // avoiding the serial pilot-search cost at large N. Uses phobic4 as
        // the inner PHF (best total-cost winner from bench_partitioned_algos).
        using part_phobic4 = partitioned_phf<phobic4>;
        print_row(run_phf_array<part_phobic4, 1>("part<phobic4>", keys, total_queries, threads));
        print_row(run_phf_array<part_phobic4, 8>("part<phobic4>", keys, total_queries, threads));
        print_row(run_phf_array<part_phobic4, 16>("part<phobic4>", keys, total_queries, threads));
        print_row(run_phf_array<part_phobic4, 32>("part<phobic4>", keys, total_queries, threads));
        print_row(run_phf_array<part_phobic4, 64>("part<phobic4>", keys, total_queries, threads));

        std::cout << '\n';
    }
    return 0;
}
