/**
 * @file bench_phf_sweep.cpp
 * @brief Parameter sweep for PHF algorithms.
 *
 * For each algorithm, sweep its primary tuning parameter at a fixed key
 * count. Reveals the best operating point per algorithm.
 *
 *   - PHOBIC: bucket_size (template param: 3, 5, 7, ...) via phobic_phf<N>
 *   - RecSplit: leaf_size (template param: 8, 16) via recsplit_hasher<N>
 *   - BBHash: gamma (runtime param via .with_gamma()) at fixed NumLevels
 *
 * Fixed key count of 50K by default (reasonable for phobic builds to complete).
 *
 * Usage:
 *   bench_phf_sweep                # 50K keys
 *   bench_phf_sweep 100000         # custom
 */

#include "bench_harness.hpp"

#include <maph/algorithms/phobic.hpp>
#include <maph/algorithms/recsplit.hpp>
#include <maph/algorithms/bbhash.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace maph;
using namespace maph::bench;

template<typename PHF, typename BuilderFactory>
result_row run_one(const std::string& name,
                   const std::vector<std::string>& keys,
                   BuilderFactory make_builder,
                   size_t total_queries) {
    using clock = std::chrono::high_resolution_clock;
    using std::chrono::duration_cast;
    using std::chrono::microseconds;

    result_row r{};
    r.algorithm = name;
    r.key_count = keys.size();
    r.ok = false;
    r.fp_rate = 0.0;

    reset_peak_rss();
    auto t0 = clock::now();
    auto built = make_builder().add_all(keys).build();
    auto t1 = clock::now();
    r.build_ms = duration_cast<microseconds>(t1 - t0).count() / 1000.0;
    r.build_peak_rss_kb = get_peak_rss_kb();

    if (!built.has_value()) return r;
    auto& phf = built.value();

    r.ok = true;
    r.range_size = phf.range_size();
    r.bits_per_key = phf.bits_per_key();
    r.memory_bytes = phf.memory_bytes();
    r.serialized_bytes = phf.serialize().size();

    auto qs = measure_queries(phf, keys, total_queries);
    r.query_median_ns = qs.median_ns;
    r.query_p99_ns = qs.p99_ns;
    r.query_mqps = qs.throughput_mqps;
    return r;
}

int main(int argc, char** argv) {
    size_t kc = 50'000;
    if (argc > 1) kc = std::stoul(argv[1]);

    const size_t total_queries = 1'000'000;

    std::cerr << "maph PHF parameter sweep at " << kc << " keys\n\n";

    auto keys = gen_random_keys(kc);

    print_tsv_header(std::cout);

    auto emit = [&](const std::string& label, auto fn) {
        std::cerr << "  " << label << " ..." << std::flush;
        auto r = fn();
        if (r.ok) std::cerr << " " << r.build_ms << "ms, " << r.bits_per_key
                            << " b/k, " << r.query_median_ns << " ns/q\n";
        else      std::cerr << " BUILD FAILED\n";
        print_tsv_row(std::cout, r);
        std::cout.flush();
    };

    // PHOBIC: sweep bucket_size. (Aliases phobic3 / phobic5 / phobic7 exist;
    // phobic_phf<4>, <6>, <8> do not, so we use the template directly.)
    std::cerr << "=== PHOBIC bucket_size sweep ===\n";
    emit("phobic<3>",  [&] { return run_one<phobic_phf<3>>("phobic<3>",  keys, [] { return phobic_phf<3>::builder{}; }, total_queries); });
    emit("phobic<4>",  [&] { return run_one<phobic_phf<4>>("phobic<4>",  keys, [] { return phobic_phf<4>::builder{}; }, total_queries); });
    emit("phobic<5>",  [&] { return run_one<phobic_phf<5>>("phobic<5>",  keys, [] { return phobic_phf<5>::builder{}; }, total_queries); });
    emit("phobic<6>",  [&] { return run_one<phobic_phf<6>>("phobic<6>",  keys, [] { return phobic_phf<6>::builder{}; }, total_queries); });
    emit("phobic<7>",  [&] { return run_one<phobic_phf<7>>("phobic<7>",  keys, [] { return phobic_phf<7>::builder{}; }, total_queries); });

    // RecSplit: sweep leaf_size (the ones that exist). Larger leaves often fail.
    std::cerr << "=== RecSplit leaf_size sweep ===\n";
    emit("recsplit<4>",  [&] { return run_one<recsplit_hasher<4>>("recsplit<4>",  keys, [] { return recsplit_hasher<4>::builder{}; }, total_queries); });
    emit("recsplit<8>",  [&] { return run_one<recsplit_hasher<8>>("recsplit<8>",  keys, [] { return recsplit_hasher<8>::builder{}; }, total_queries); });
    emit("recsplit<12>", [&] { return run_one<recsplit_hasher<12>>("recsplit<12>", keys, [] { return recsplit_hasher<12>::builder{}; }, total_queries); });
    emit("recsplit<16>", [&] { return run_one<recsplit_hasher<16>>("recsplit<16>", keys, [] { return recsplit_hasher<16>::builder{}; }, total_queries); });

    // BBHash: sweep gamma at NumLevels=3 and NumLevels=5.
    std::cerr << "=== BBHash gamma sweep ===\n";
    for (double g : {1.5, 2.0, 2.5, 3.0}) {
        std::string label = "bbhash3,gamma=" + std::to_string(g).substr(0, 3);
        emit(label, [&, g] {
            return run_one<bbhash3>(label, keys,
                [g] { return bbhash3::builder{}.with_gamma(g); },
                total_queries);
        });
    }
    for (double g : {1.5, 2.0, 2.5}) {
        std::string label = "bbhash5,gamma=" + std::to_string(g).substr(0, 3);
        emit(label, [&, g] {
            return run_one<bbhash5>(label, keys,
                [g] { return bbhash5::builder{}.with_gamma(g); },
                total_queries);
        });
    }

    return 0;
}
