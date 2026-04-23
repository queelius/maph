/**
 * @file bench_approximate_map.cpp
 * @brief Benchmark suite for approximate_map implementations.
 *
 * An approximate_map composes structural identification with approximate
 * membership. Two patterns in the library satisfy this:
 *
 *   perfect_filter<PHF, FPBits>
 *     - Query: contains(k) = yes/no; slot_for(k) = optional slot index in [0, n)
 *     - Space: PHF.bits_per_key + FPBits
 *     - Use: when you already need a unique slot to index into a separate
 *       values array you maintain yourself
 *
 *   bloomier<Retrieval, Oracle>
 *     - Query: contains(k) = yes/no; lookup(k) = optional M-bit value
 *     - Space: Retrieval.bits_per_key + Oracle.bits_per_key
 *     - Use: when the value can be folded into the structure itself
 *
 * This benchmark runs both patterns side by side so you can compare them
 * on the same axes: build time, total bits/key, contains() latency, and
 * empirical FPR. Different information content on hit (slot vs value)
 * but same answer-shape on miss (nullopt).
 */

#include "bench_harness.hpp"

#include <maph/algorithms/bbhash.hpp>
#include <maph/algorithms/phobic.hpp>
#include <maph/algorithms/recsplit.hpp>
#include <maph/composition/bloomier.hpp>
#include <maph/composition/perfect_filter.hpp>
#include <maph/filters/binary_fuse_filter.hpp>
#include <maph/filters/ribbon_filter.hpp>
#include <maph/filters/xor_filter.hpp>
#include <maph/retrieval/phf_value_array.hpp>
#include <maph/retrieval/ribbon_retrieval.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace maph;
using namespace maph::bench;

namespace {

std::vector<std::string> gen_unknown_keys(size_t count, uint64_t seed) {
    std::vector<std::string> keys;
    keys.reserve(count);
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<int> byte_dist(0, 255);
    for (size_t i = 0; i < count; ++i) {
        std::string key = "UNKN";
        key.resize(16, '\0');
        for (size_t j = 4; j < 16; ++j) key[j] = static_cast<char>(byte_dist(rng));
        keys.push_back(std::move(key));
    }
    return keys;
}

// perfect_filter::contains(key) is the oracle-style API we benchmark here.
template<typename Map>
query_stats measure_map(const Map& m,
                        const std::vector<std::string>& keys,
                        size_t total_queries = 1'000'000,
                        size_t sub_batch_size = 1000,
                        uint64_t seed = 12345) {
    using clock = std::chrono::high_resolution_clock;
    using std::chrono::duration_cast;
    using std::chrono::nanoseconds;

    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<size_t> kd(0, keys.size() - 1);
    std::vector<size_t> indices(total_queries);
    for (auto& i : indices) i = kd(rng);

    for (size_t i = 0; i < 10000 && i < total_queries; ++i) {
        consume(m.contains(keys[indices[i]]));
    }

    const size_t M = total_queries / sub_batch_size;
    std::vector<double> batch_ns;
    batch_ns.reserve(M);

    auto global_start = clock::now();
    for (size_t i = 0; i < M; ++i) {
        size_t base = i * sub_batch_size;
        auto t0 = clock::now();
        for (size_t b = 0; b < sub_batch_size; ++b) {
            consume(m.contains(keys[indices[base + b]]));
        }
        auto t1 = clock::now();
        batch_ns.push_back(
            static_cast<double>(duration_cast<nanoseconds>(t1 - t0).count())
            / static_cast<double>(sub_batch_size));
    }
    auto global_end = clock::now();

    std::sort(batch_ns.begin(), batch_ns.end());
    double median = batch_ns[M / 2];
    double p99 = batch_ns[static_cast<size_t>(M * 0.99)];
    double total_ns = static_cast<double>(
        duration_cast<nanoseconds>(global_end - global_start).count());
    double mqps = (static_cast<double>(M * sub_batch_size) / total_ns) * 1000.0;
    return {median, p99, mqps};
}

// Deterministic value-from-key for bloomier build. Same shape as
// bench_bloomier's so numbers line up.
inline uint64_t det_value(std::string_view key) noexcept {
    uint64_t h = 0x9e3779b97f4a7c15ULL;
    for (char c : key) {
        h ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        h *= 0xbf58476d1ce4e5b9ULL;
        h ^= h >> 31;
    }
    return h;
}

template<typename PHF, unsigned FPBits>
result_row run_perfect_filter(const std::string& phf_name,
                              const std::vector<std::string>& keys,
                              const std::vector<std::string>& unknowns,
                              size_t total_queries) {
    using clock = std::chrono::high_resolution_clock;
    using std::chrono::duration_cast;
    using std::chrono::microseconds;
    using Map = perfect_filter<PHF, FPBits>;

    result_row r{};
    r.algorithm = phf_name + "+pf" + std::to_string(FPBits);
    r.key_count = keys.size();
    r.ok = false;

    reset_peak_rss();
    auto t0 = clock::now();
    auto phf_built = typename PHF::builder{}.add_all(keys).build();
    if (!phf_built.has_value()) {
        auto t1 = clock::now();
        r.build_ms = duration_cast<microseconds>(t1 - t0).count() / 1000.0;
        r.build_peak_rss_kb = get_peak_rss_kb();
        return r;
    }
    auto map = Map::build(std::move(*phf_built), keys);
    auto t1 = clock::now();
    r.build_ms = duration_cast<microseconds>(t1 - t0).count() / 1000.0;
    r.build_peak_rss_kb = get_peak_rss_kb();

    r.ok = true;
    r.range_size = map.range_size();
    r.bits_per_key = map.phf().bits_per_key() + static_cast<double>(FPBits);
    r.memory_bytes = map.phf().memory_bytes()
                   + (keys.size() * FPBits + 7) / 8;  // approx
    r.serialized_bytes = map.serialize().size();

    auto qs = measure_map(map, keys, total_queries);
    r.query_median_ns = qs.median_ns;
    r.query_p99_ns = qs.p99_ns;
    r.query_mqps = qs.throughput_mqps;

    // Empirical FPR on unknown keys.
    size_t fp = 0;
    for (const auto& k : unknowns) {
        if (map.contains(k)) ++fp;
    }
    r.fp_rate = static_cast<double>(fp) / static_cast<double>(unknowns.size());
    return r;
}

// Run a bloomier<Retrieval, Oracle> composition and record the same
// axes as run_perfect_filter so the two can be compared row by row.
template<typename Retrieval, typename Oracle, unsigned ValueBits, unsigned FPBits>
result_row run_bloomier_composition(const std::string& name,
                                    const std::vector<std::string>& keys,
                                    const std::vector<std::string>& unknowns,
                                    size_t total_queries) {
    using clock = std::chrono::high_resolution_clock;
    using std::chrono::duration_cast;
    using std::chrono::microseconds;
    using Map = bloomier<Retrieval, Oracle>;

    result_row r{};
    r.algorithm = name;
    r.key_count = keys.size();
    r.ok = false;

    reset_peak_rss();
    auto t0 = clock::now();
    auto built = typename Map::builder{}
        .add_all_with(std::span<const std::string>{keys},
                      [](std::string_view k) { return det_value(k); })
        .build();
    auto t1 = clock::now();
    r.build_ms = duration_cast<microseconds>(t1 - t0).count() / 1000.0;
    r.build_peak_rss_kb = get_peak_rss_kb();
    if (!built.has_value()) return r;

    r.ok = true;
    // bloomier wraps a retrieval (no slots); report num_keys as range for
    // comparability.
    r.range_size = built->num_keys();
    r.bits_per_key = built->bits_per_key();
    r.memory_bytes = built->memory_bytes();
    r.serialized_bytes = built->serialize().size();

    auto qs = measure_map(*built, keys, total_queries);
    r.query_median_ns = qs.median_ns;
    r.query_p99_ns = qs.p99_ns;
    r.query_mqps = qs.throughput_mqps;

    size_t fp = 0;
    for (const auto& k : unknowns) {
        if (built->contains(k)) ++fp;
    }
    r.fp_rate = static_cast<double>(fp) / static_cast<double>(unknowns.size());
    return r;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<size_t> key_counts = {10'000, 100'000};
    if (argc > 1) {
        key_counts.clear();
        for (int i = 1; i < argc; ++i) key_counts.push_back(std::stoul(argv[i]));
    }

    const size_t total_queries = 1'000'000;
    const size_t num_unknowns = 100'000;

    std::cerr << "maph approximate_map benchmark (perfect_filter)\n"
              << "  key counts: ";
    for (auto kc : key_counts) std::cerr << kc << ' ';
    std::cerr << "\n  queries per measurement: " << total_queries << "\n"
              << "  unknown keys for FPR: " << num_unknowns << "\n\n";

    print_tsv_header(std::cout);

    for (size_t kc : key_counts) {
        std::cerr << "=== " << kc << " keys ===\n";
        auto keys = gen_random_keys(kc);
        auto unknowns = gen_unknown_keys(num_unknowns, 99999);

        auto do_one = [&](const std::string& name, auto run_fn) {
            std::cerr << "  " << name << " ..." << std::flush;
            auto r = run_fn();
            if (r.ok) std::cerr << " " << r.build_ms << "ms, "
                                << r.bits_per_key << " b/k, "
                                << r.query_median_ns << " ns/q, fp="
                                << r.fp_rate << "\n";
            else      std::cerr << " BUILD FAILED\n";
            print_tsv_row(std::cout, r);
            std::cout.flush();
        };

        // === perfect_filter (slot-returning approximate maps) ===
        do_one("phobic5+pf8",   [&]{ return run_perfect_filter<phobic5,    8>("phobic5",   keys, unknowns, total_queries); });
        do_one("phobic5+pf16",  [&]{ return run_perfect_filter<phobic5,   16>("phobic5",   keys, unknowns, total_queries); });
        do_one("phobic5+pf32",  [&]{ return run_perfect_filter<phobic5,   32>("phobic5",   keys, unknowns, total_queries); });

        do_one("phobic3+pf16",  [&]{ return run_perfect_filter<phobic3,   16>("phobic3",   keys, unknowns, total_queries); });

        do_one("recsplit8+pf16",[&]{ return run_perfect_filter<recsplit8,  16>("recsplit8", keys, unknowns, total_queries); });

        do_one("bbhash3+pf16",  [&]{ return run_perfect_filter<bbhash3,   16>("bbhash3",   keys, unknowns, total_queries); });
        do_one("bbhash5+pf16",  [&]{ return run_perfect_filter<bbhash5,   16>("bbhash5",   keys, unknowns, total_queries); });

        // === bloomier (value-returning approximate maps) ===
        // Same FPR (2^-8 or 2^-16) and value width (M=16) so the rows
        // line up with perfect_filter on build time, space, and latency.
        do_one("bloomier<rib16,xor8>",
            [&]{ return run_bloomier_composition<ribbon_retrieval<16>, xor_filter<8>, 16, 8>(
                    "bloomier<rib16,xor8>", keys, unknowns, total_queries); });
        do_one("bloomier<rib16,binfuse8>",
            [&]{ return run_bloomier_composition<ribbon_retrieval<16>, binary_fuse_filter<8>, 16, 8>(
                    "bloomier<rib16,binfuse8>", keys, unknowns, total_queries); });
        do_one("bloomier<rib16,rib8>",
            [&]{ return run_bloomier_composition<ribbon_retrieval<16>, ribbon_filter<8>, 16, 8>(
                    "bloomier<rib16,rib8>", keys, unknowns, total_queries); });
        do_one("bloomier<rib16,binfuse16>",
            [&]{ return run_bloomier_composition<ribbon_retrieval<16>, binary_fuse_filter<16>, 16, 16>(
                    "bloomier<rib16,binfuse16>", keys, unknowns, total_queries); });
        do_one("bloomier<pva<phobic5>16,xor8>",
            [&]{ return run_bloomier_composition<phf_value_array<phobic5, 16>, xor_filter<8>, 16, 8>(
                    "bloomier<pva<phobic5>16,xor8>", keys, unknowns, total_queries); });
        // Narrow-M cipher-map-flavored row: 1-bit value + 8-bit FPR.
        do_one("bloomier<rib1,binfuse8>",
            [&]{ return run_bloomier_composition<ribbon_retrieval<1>, binary_fuse_filter<8>, 1, 8>(
                    "bloomier<rib1,binfuse8>", keys, unknowns, total_queries); });
    }
    return 0;
}
