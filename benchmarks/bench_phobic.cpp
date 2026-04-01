/**
 * @file bench_phobic.cpp
 * @brief Fair benchmark comparing all PHF algorithms
 *
 * Reports pure hash structure bits/key (no fingerprints), query latency,
 * and build time. Also benchmarks perfect_filter<Algorithm, 16> variants
 * for membership verification cost.
 */

#include <maph/core.hpp>
#include <maph/hashers_perfect.hpp>
#include <maph/phobic.hpp>
#include <maph/perfect_filter.hpp>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <random>
#include <algorithm>
#include <numeric>

using namespace maph;
using namespace std::chrono;

std::vector<std::string> gen_keys(size_t count, uint64_t seed = 42) {
    std::vector<std::string> keys;
    keys.reserve(count);
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<int> byte_dist(0, 255);
    for (size_t i = 0; i < count; ++i) {
        std::string key(16, '\0');
        for (auto& c : key) c = static_cast<char>(byte_dist(rng));
        keys.push_back(std::move(key));
    }
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    return keys;
}

struct bench_result {
    std::string algorithm;
    size_t key_count;
    double build_ms;
    double query_median_ns;
    double query_p99_ns;
    double bits_per_key;
    size_t memory_bytes;
};

void print_header() {
    std::cout << "algorithm\tkeys\tbuild_ms\tquery_med_ns\tquery_p99_ns\tbits_per_key\tmem_kb\n";
}

void print_result(const bench_result& r) {
    std::cout << std::fixed
              << r.algorithm << '\t' << r.key_count << '\t'
              << std::setprecision(2) << r.build_ms << '\t'
              << r.query_median_ns << '\t' << r.query_p99_ns << '\t'
              << r.bits_per_key << '\t'
              << std::setprecision(1) << (r.memory_bytes / 1024.0) << '\n';
}

template<typename Fn>
std::pair<double, double> measure_query(Fn&& fn, size_t iters) {
    std::vector<double> times;
    times.reserve(iters);
    for (size_t i = 0; i < iters; ++i) {
        auto t0 = high_resolution_clock::now();
        fn(i);
        auto t1 = high_resolution_clock::now();
        times.push_back(static_cast<double>(duration_cast<nanoseconds>(t1 - t0).count()));
    }
    std::sort(times.begin(), times.end());
    return {times[times.size() / 2], times[static_cast<size_t>(times.size() * 0.99)]};
}

/// Benchmark a pure PHF algorithm
template<typename PHFType>
void bench_phf(const std::string& name,
               auto make_builder,
               const std::vector<std::string>& keys,
               std::mt19937_64& rng,
               std::uniform_int_distribution<size_t>& kd,
               size_t qi)
{
    auto t0 = high_resolution_clock::now();
    auto phf = make_builder().add_all(keys).build();
    auto t1 = high_resolution_clock::now();
    if (phf) {
        auto rng_copy = rng;
        auto [med, p99] = measure_query([&](size_t) {
            volatile auto s = phf->slot_for(keys[kd(rng_copy)]); (void)s;
        }, qi);
        print_result({name, keys.size(),
            duration_cast<microseconds>(t1 - t0).count() / 1000.0,
            med, p99, phf->bits_per_key(), phf->memory_bytes()});
    } else {
        std::cerr << name << " build FAILED\n";
    }
}

/// Benchmark a PHF + perfect_filter<16> variant
template<typename PHFType>
void bench_phf_with_filter(const std::string& name,
                           auto make_builder,
                           const std::vector<std::string>& keys,
                           std::mt19937_64& rng,
                           std::uniform_int_distribution<size_t>& kd,
                           size_t qi)
{
    auto t0 = high_resolution_clock::now();
    auto phf = make_builder().add_all(keys).build();
    if (phf) {
        auto pf = perfect_filter<PHFType, 16>::build(std::move(*phf), keys);
        auto t1 = high_resolution_clock::now();
        auto rng_copy = rng;
        auto [med, p99] = measure_query([&](size_t) {
            volatile auto s = pf.slot_for(keys[kd(rng_copy)]); (void)s;
        }, qi);
        double bpk = pf.phf().bits_per_key() + 16.0;
        size_t mem = pf.phf().memory_bytes() + keys.size() * 2;
        print_result({name + "+pf16", keys.size(),
            duration_cast<microseconds>(t1 - t0).count() / 1000.0,
            med, p99, bpk, mem});
    } else {
        std::cerr << name << "+pf16 build FAILED\n";
    }
}

int main(int argc, char** argv) {
    std::vector<size_t> key_counts = {10000, 100000, 1000000};
    size_t qi = 1000000;

    if (argc > 1) {
        key_counts.clear();
        for (int i = 1; i < argc; ++i) key_counts.push_back(std::stoul(argv[i]));
    }

    std::cerr << "PHOBIC Benchmark — All PHF Algorithms\n\n";
    print_header();

    for (size_t kc : key_counts) {
        auto keys = gen_keys(kc);
        std::cerr << "=== " << keys.size() << " keys ===\n";

        std::mt19937_64 rng{123};
        std::uniform_int_distribution<size_t> kd(0, keys.size() - 1);

        // --- PHOBIC ---
        bench_phf<phobic5>("phobic5",
            []{ return phobic5::builder{}; },
            keys, rng, kd, qi);
        bench_phf_with_filter<phobic5>("phobic5",
            []{ return phobic5::builder{}; },
            keys, rng, kd, qi);

        // --- RecSplit ---
        bench_phf<recsplit8>("recsplit8",
            []{ return recsplit8::builder{}; },
            keys, rng, kd, qi);
        bench_phf_with_filter<recsplit8>("recsplit8",
            []{ return recsplit8::builder{}; },
            keys, rng, kd, qi);

        // --- CHD ---
        bench_phf<chd_hasher>("chd",
            []{ return chd_hasher::builder{}; },
            keys, rng, kd, qi);
        bench_phf_with_filter<chd_hasher>("chd",
            []{ return chd_hasher::builder{}; },
            keys, rng, kd, qi);

        // --- BBHash ---
        bench_phf<bbhash3>("bbhash3",
            []{ return bbhash3::builder{}; },
            keys, rng, kd, qi);
        bench_phf_with_filter<bbhash3>("bbhash3",
            []{ return bbhash3::builder{}; },
            keys, rng, kd, qi);

        // --- FCH ---
        bench_phf<fch_hasher>("fch",
            []{ return fch_hasher::builder{}; },
            keys, rng, kd, qi);
        bench_phf_with_filter<fch_hasher>("fch",
            []{ return fch_hasher::builder{}; },
            keys, rng, kd, qi);

        // --- PTHash (may fail at larger key counts) ---
        bench_phf<pthash98>("pthash98",
            []{ return pthash98::builder{}; },
            keys, rng, kd, qi);
    }

    return 0;
}
