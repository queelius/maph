/**
 * @file bench_approximate_map.cpp
 * @brief Benchmark suite for approximate_map implementations.
 *
 * An approximate_map composes a PHF with a membership oracle. For keys
 * in the build set, slot_for(key) returns the unique slot. For unknowns,
 * slot_for() returns nullopt with bounded FPR. perfect_filter<PHF, FPBits>
 * is the canonical instance.
 *
 * Compares PHF backends (phobic, recsplit, bbhash) x fingerprint widths
 * (8, 16, 32) on the four axes that matter for an approximate map:
 *   - total bits/key (PHF structure + fingerprint array)
 *   - build time
 *   - contains() query latency
 *   - empirical FPR
 */

#include "bench_harness.hpp"

#include <maph/algorithms/phobic.hpp>
#include <maph/algorithms/recsplit.hpp>
#include <maph/algorithms/bbhash.hpp>
#include <maph/composition/perfect_filter.hpp>

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

        do_one("phobic5+pf8",   [&]{ return run_perfect_filter<phobic5,    8>("phobic5",   keys, unknowns, total_queries); });
        do_one("phobic5+pf16",  [&]{ return run_perfect_filter<phobic5,   16>("phobic5",   keys, unknowns, total_queries); });
        do_one("phobic5+pf32",  [&]{ return run_perfect_filter<phobic5,   32>("phobic5",   keys, unknowns, total_queries); });

        do_one("phobic3+pf16",  [&]{ return run_perfect_filter<phobic3,   16>("phobic3",   keys, unknowns, total_queries); });

        do_one("recsplit8+pf16",[&]{ return run_perfect_filter<recsplit8,  16>("recsplit8", keys, unknowns, total_queries); });

        do_one("bbhash3+pf16",  [&]{ return run_perfect_filter<bbhash3,   16>("bbhash3",   keys, unknowns, total_queries); });
        do_one("bbhash5+pf16",  [&]{ return run_perfect_filter<bbhash5,   16>("bbhash5",   keys, unknowns, total_queries); });
    }
    return 0;
}
