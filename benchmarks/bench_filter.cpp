/**
 * @file bench_filter.cpp
 * @brief Benchmark suite for membership_oracle implementations.
 *
 * A membership_oracle answers "is this key in the set?" with a bounded
 * false positive rate and no dependency on external structures (unlike
 * packed_fingerprint_array, which needs a PHF to provide slot indices).
 *
 * Metrics per (algorithm x fingerprint_bits x key_count):
 *   - build time (ms) and peak RSS (kB)
 *   - bits per key (actual, including any structural overhead)
 *   - in-memory bytes, serialized bytes
 *   - verify() median/p99 latency (ns/query), throughput (MQPS)
 *   - empirical false positive rate (unknown keys misreported as members)
 *
 * Usage:
 *   bench_filter                    # default: 10000 100000
 *   bench_filter 1000 50000         # custom
 */

#include "bench_harness.hpp"

#include <maph/filters/xor_filter.hpp>
#include <maph/filters/ribbon_filter.hpp>

#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using namespace maph;
using namespace maph::bench;

namespace {

// Local helper: generate "unknown" keys distinct from the build set by prefix.
std::vector<std::string> gen_unknown_keys(size_t count, uint64_t seed) {
    std::vector<std::string> keys;
    keys.reserve(count);
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<int> byte_dist(0, 255);
    for (size_t i = 0; i < count; ++i) {
        std::string key = "UNKN";  // guaranteed not to collide with gen_random_keys
        key.resize(16, '\0');
        for (size_t j = 4; j < 16; ++j) key[j] = static_cast<char>(byte_dist(rng));
        keys.push_back(std::move(key));
    }
    return keys;
}

// verify() on a membership_oracle takes just the key.
template<typename Oracle>
query_stats measure_oracle(const Oracle& o,
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
        consume(o.verify(keys[indices[i]]));
    }

    const size_t M = total_queries / sub_batch_size;
    std::vector<double> batch_ns;
    batch_ns.reserve(M);

    auto global_start = clock::now();
    for (size_t m = 0; m < M; ++m) {
        size_t base = m * sub_batch_size;
        auto t0 = clock::now();
        for (size_t b = 0; b < sub_batch_size; ++b) {
            consume(o.verify(keys[indices[base + b]]));
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

template<typename Oracle>
double measure_fpr(const Oracle& o, const std::vector<std::string>& unknowns) {
    size_t fp = 0;
    for (const auto& k : unknowns) {
        if (o.verify(k)) ++fp;
    }
    return static_cast<double>(fp) / static_cast<double>(unknowns.size());
}

// Wrap build + measure for any oracle. Builder returns bool (success).
template<typename Oracle, typename BuildFn>
result_row run_oracle(const std::string& name,
                     unsigned fp_bits,
                     const std::vector<std::string>& keys,
                     const std::vector<std::string>& unknowns,
                     BuildFn&& build_into,
                     size_t total_queries) {
    using clock = std::chrono::high_resolution_clock;
    using std::chrono::duration_cast;
    using std::chrono::microseconds;

    result_row r{};
    r.algorithm = name + "<" + std::to_string(fp_bits) + ">";
    r.key_count = keys.size();
    r.range_size = 0;  // oracles don't have a slot range
    r.ok = false;

    Oracle o;
    reset_peak_rss();
    auto t0 = clock::now();
    bool built = build_into(o);
    auto t1 = clock::now();
    r.build_ms = duration_cast<microseconds>(t1 - t0).count() / 1000.0;
    r.build_peak_rss_kb = get_peak_rss_kb();

    if (!built) return r;

    r.ok = true;
    r.bits_per_key = o.bits_per_key(keys.size());
    r.memory_bytes = o.memory_bytes();
    r.serialized_bytes = o.serialize().size();

    auto qs = measure_oracle(o, keys, total_queries);
    r.query_median_ns = qs.median_ns;
    r.query_p99_ns = qs.p99_ns;
    r.query_mqps = qs.throughput_mqps;

    r.fp_rate = measure_fpr(o, unknowns);
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
    const size_t num_unknowns = 100'000;  // for FPR estimation

    std::cerr << "maph membership_oracle benchmark\n"
              << "  key counts: ";
    for (auto kc : key_counts) std::cerr << kc << ' ';
    std::cerr << "\n  queries per measurement: " << total_queries << "\n"
              << "  unknown keys for FPR: " << num_unknowns << "\n\n";

    print_tsv_header(std::cout);

    for (size_t kc : key_counts) {
        std::cerr << "=== " << kc << " keys ===\n";
        auto keys = gen_random_keys(kc);
        auto unknowns = gen_unknown_keys(num_unknowns, 99999);

        auto run_xor = [&](auto tag, unsigned bits) {
            using F = xor_filter<decltype(tag)::value>;
            std::cerr << "  xor<" << bits << "> ..." << std::flush;
            auto r = run_oracle<F>(
                "xor", bits, keys, unknowns,
                [&](F& o) { return o.build(keys); },
                total_queries);
            if (r.ok) std::cerr << " " << r.build_ms << "ms, " << r.bits_per_key
                                << " b/k, " << r.query_median_ns << " ns/q, fp="
                                << r.fp_rate << "\n";
            else      std::cerr << " BUILD FAILED\n";
            print_tsv_row(std::cout, r);
            std::cout.flush();
        };

        auto run_ribbon = [&](auto tag, unsigned bits) {
            using F = ribbon_filter<decltype(tag)::value>;
            std::cerr << "  ribbon<" << bits << "> ..." << std::flush;
            auto r = run_oracle<F>(
                "ribbon", bits, keys, unknowns,
                [&](F& o) { return o.build(keys); },
                total_queries);
            if (r.ok) std::cerr << " " << r.build_ms << "ms, " << r.bits_per_key
                                << " b/k, " << r.query_median_ns << " ns/q, fp="
                                << r.fp_rate << "\n";
            else      std::cerr << " BUILD FAILED\n";
            print_tsv_row(std::cout, r);
            std::cout.flush();
        };

        run_xor(std::integral_constant<unsigned, 8>{}, 8);
        run_xor(std::integral_constant<unsigned, 16>{}, 16);
        run_xor(std::integral_constant<unsigned, 32>{}, 32);
        run_ribbon(std::integral_constant<unsigned, 8>{}, 8);
        run_ribbon(std::integral_constant<unsigned, 16>{}, 16);
        run_ribbon(std::integral_constant<unsigned, 32>{}, 32);
    }
    return 0;
}
