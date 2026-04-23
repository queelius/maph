/**
 * @file bench_bloomier.cpp
 * @brief Compare bloomier<Retrieval, Oracle> compositions.
 *
 * bloomier returns Optional<value_type> — the retrieval provides the
 * value, the oracle gates membership. This benchmark sweeps pairs of
 * (retrieval type, oracle type) at a chosen value width M and FPR-bit
 * width, reporting space, build time, query latency, and empirical FPR.
 *
 * The grid answers practical questions:
 *   - Does ribbon_retrieval + binary_fuse_filter beat pva + packed
 *     fingerprint on space?
 *   - Which oracle keeps up best with ribbon_retrieval's query speed?
 *   - Where is the build-time bottleneck (retrieval or oracle)?
 *
 * Usage:
 *   bench_bloomier                       # default 100K + 1M keys
 *   bench_bloomier --keys=10000,100000   # custom sizes
 */

#include "bench_harness.hpp"

#include <maph/algorithms/phobic.hpp>
#include <maph/composition/bloomier.hpp>
#include <maph/filters/binary_fuse_filter.hpp>
#include <maph/filters/ribbon_filter.hpp>
#include <maph/filters/xor_filter.hpp>
#include <maph/retrieval/phf_value_array.hpp>
#include <maph/retrieval/ribbon_retrieval.hpp>

#include <chrono>
#include <cstdint>
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
    size_t value_bits;
    size_t oracle_bits;
    size_t keys;
    double build_ms;
    double bits_per_key;
    size_t memory_kb;
    double query_median_ns;
    double fp_rate;
    bool ok;
};

uint64_t det_value(std::string_view key) noexcept {
    uint64_t h = 0x9e3779b97f4a7c15ULL;
    for (char c : key) {
        h ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        h *= 0xbf58476d1ce4e5b9ULL;
        h ^= h >> 31;
    }
    return h;
}

std::vector<std::string> gen_unknown_for_bloomier(size_t count, uint64_t seed) {
    std::vector<std::string> out;
    out.reserve(count);
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<int> b(0, 255);
    for (size_t i = 0; i < count; ++i) {
        std::string k = "UNBL";
        k.resize(16, '\0');
        for (size_t j = 4; j < 16; ++j) k[j] = static_cast<char>(b(rng));
        out.push_back(std::move(k));
    }
    return out;
}

template <typename B>
row run_one(const std::string& name, unsigned M, unsigned fp_bits,
            const std::vector<std::string>& keys,
            size_t total_queries) {
    using clock = std::chrono::high_resolution_clock;
    using std::chrono::duration_cast;
    using std::chrono::microseconds;
    using std::chrono::nanoseconds;

    row r{};
    r.config = name;
    r.value_bits = M;
    r.oracle_bits = fp_bits;
    r.keys = keys.size();
    r.ok = false;

    std::cerr << "  " << name << " ..." << std::flush;

    auto t0 = clock::now();
    auto builder = typename B::builder{};
    builder.add_all_with(std::span<const std::string>{keys},
                         [](std::string_view k) { return det_value(k); });
    auto built = builder.build();
    auto t1 = clock::now();
    r.build_ms = static_cast<double>(duration_cast<microseconds>(t1 - t0).count()) / 1000.0;
    if (!built.has_value()) {
        std::cerr << " BUILD FAILED\n";
        return r;
    }
    r.ok = true;
    r.bits_per_key = built->bits_per_key();
    r.memory_kb = built->memory_bytes() / 1024;

    // Query latency on in-set keys (hot path).
    static constexpr size_t inner = 1000;
    size_t outer = total_queries / inner;
    if (outer == 0) outer = 1;
    std::vector<double> per_batch;
    per_batch.reserve(outer);
    uint64_t sink = 0;
    size_t idx = 0;
    for (size_t i = 0; i < outer; ++i) {
        auto a = clock::now();
        for (size_t j = 0; j < inner; ++j) {
            auto v = built->lookup(keys[idx]);
            sink ^= v.has_value() ? static_cast<uint64_t>(*v) : 0;
            idx++;
            if (idx >= keys.size()) idx = 0;
        }
        auto b = clock::now();
        per_batch.push_back(
            static_cast<double>(duration_cast<nanoseconds>(b - a).count()) / inner);
    }
    consume(sink != 0);
    std::sort(per_batch.begin(), per_batch.end());
    r.query_median_ns = per_batch[per_batch.size() / 2];

    // Empirical FPR on a disjoint unknown set.
    auto unknowns = gen_unknown_for_bloomier(20000, 99999);
    size_t fp = 0;
    for (const auto& u : unknowns) {
        if (built->lookup(u).has_value()) ++fp;
    }
    r.fp_rate = static_cast<double>(fp) / unknowns.size();

    std::cerr << " " << r.build_ms << " ms, " << r.bits_per_key
              << " b/k, " << r.query_median_ns << " ns/q, fp="
              << r.fp_rate << "\n";
    return r;
}

void print_header() {
    std::cout << std::left
        << std::setw(40) << "config"
        << std::right
        << std::setw(6)  << "M"
        << std::setw(7)  << "fp_bit"
        << std::setw(10) << "keys"
        << std::setw(12) << "build_ms"
        << std::setw(14) << "bits_per_key"
        << std::setw(10) << "mem_kb"
        << std::setw(12) << "query_ns"
        << std::setw(12) << "fp_rate"
        << std::setw(5)  << "ok"
        << '\n';
}

void print_row(const row& r) {
    std::cout << std::left
        << std::setw(40) << r.config
        << std::right << std::fixed
        << std::setw(6)  << r.value_bits
        << std::setw(7)  << r.oracle_bits
        << std::setw(10) << r.keys
        << std::setw(12) << std::setprecision(2) << r.build_ms
        << std::setw(14) << std::setprecision(3) << r.bits_per_key
        << std::setw(10) << r.memory_kb
        << std::setw(12) << std::setprecision(2) << r.query_median_ns
        << std::setw(12) << std::setprecision(6) << r.fp_rate
        << std::setw(5)  << (r.ok ? "1" : "0")
        << '\n';
    std::cout.flush();
}

} // namespace

int main(int argc, char** argv) {
    cli_args args(argc, argv);
    auto key_counts = args.get_size_list("keys", {100'000, 1'000'000});
    size_t total_queries = args.get_size("queries", 300'000);
    std::string dist = args.get_string("distribution", "random");

    std::cerr << "bench_bloomier: retrieval x oracle comparison\n"
              << "  keys:";
    for (auto k : key_counts) std::cerr << ' ' << k;
    std::cerr << "\n  queries: " << total_queries
              << "\n  distribution: " << dist << "\n\n";

    print_header();

    for (size_t kc : key_counts) {
        std::cerr << "=== " << kc << " keys (" << dist << ") ===\n";
        auto keys = gen_keys_by_name(dist, kc);
        std::cerr << "  " << keys.size() << " unique keys\n";

        // M=16 value, 8-bit oracle (~0.4% FPR).
        print_row(run_one<bloomier<ribbon_retrieval<16>, xor_filter<8>>>(
            "ribbon<16> + xor<8>", 16, 8, keys, total_queries));
        print_row(run_one<bloomier<ribbon_retrieval<16>, ribbon_filter<8>>>(
            "ribbon<16> + ribbon<8>", 16, 8, keys, total_queries));
        print_row(run_one<bloomier<ribbon_retrieval<16>, binary_fuse_filter<8>>>(
            "ribbon<16> + binary_fuse<8>", 16, 8, keys, total_queries));
        print_row(run_one<bloomier<phf_value_array<phobic5, 16>, xor_filter<8>>>(
            "pva<phobic5,16> + xor<8>", 16, 8, keys, total_queries));
        print_row(run_one<bloomier<phf_value_array<phobic5, 16>, binary_fuse_filter<8>>>(
            "pva<phobic5,16> + binary_fuse<8>", 16, 8, keys, total_queries));

        std::cout << '\n';

        // M=16 value, 16-bit oracle (~0.0015% FPR).
        print_row(run_one<bloomier<ribbon_retrieval<16>, xor_filter<16>>>(
            "ribbon<16> + xor<16>", 16, 16, keys, total_queries));
        print_row(run_one<bloomier<ribbon_retrieval<16>, ribbon_filter<16>>>(
            "ribbon<16> + ribbon<16>", 16, 16, keys, total_queries));
        print_row(run_one<bloomier<ribbon_retrieval<16>, binary_fuse_filter<16>>>(
            "ribbon<16> + binary_fuse<16>", 16, 16, keys, total_queries));

        std::cout << '\n';

        // Varying M with best oracle (binary_fuse<8>).
        print_row(run_one<bloomier<ribbon_retrieval<1>, binary_fuse_filter<8>>>(
            "ribbon<1> + binary_fuse<8>", 1, 8, keys, total_queries));
        print_row(run_one<bloomier<ribbon_retrieval<8>, binary_fuse_filter<8>>>(
            "ribbon<8> + binary_fuse<8>", 8, 8, keys, total_queries));
        print_row(run_one<bloomier<ribbon_retrieval<32>, binary_fuse_filter<8>>>(
            "ribbon<32> + binary_fuse<8>", 32, 8, keys, total_queries));
        print_row(run_one<bloomier<ribbon_retrieval<64>, binary_fuse_filter<8>>>(
            "ribbon<64> + binary_fuse<8>", 64, 8, keys, total_queries));

        std::cout << '\n';
    }
    return 0;
}
