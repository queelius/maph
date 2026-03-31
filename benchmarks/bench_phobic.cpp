/**
 * @file bench_phobic.cpp
 * @brief Benchmark PHOBIC against existing perfect hash algorithms
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

int main(int argc, char** argv) {
    std::vector<size_t> key_counts = {10000, 100000, 1000000};
    size_t qi = 1000000;

    if (argc > 1) {
        key_counts.clear();
        for (int i = 1; i < argc; ++i) key_counts.push_back(std::stoul(argv[i]));
    }

    std::cerr << "PHOBIC Benchmark\n\n";
    print_header();

    for (size_t kc : key_counts) {
        auto keys = gen_keys(kc);
        std::cerr << "=== " << keys.size() << " keys ===\n";

        std::mt19937_64 rng{123};
        std::uniform_int_distribution<size_t> kd(0, keys.size() - 1);

        // PHOBIC (pure PHF)
        {
            auto t0 = high_resolution_clock::now();
            auto phf = phobic5::builder{}.add_all(keys).build();
            auto t1 = high_resolution_clock::now();
            if (phf) {
                auto rng_copy = rng;
                auto [med, p99] = measure_query([&](size_t) {
                    volatile auto s = phf->slot_for(keys[kd(rng_copy)]); (void)s;
                }, qi);
                print_result({"phobic5", keys.size(),
                    duration_cast<microseconds>(t1 - t0).count() / 1000.0,
                    med, p99, phf->bits_per_key(), phf->memory_bytes()});
            } else {
                std::cerr << "phobic5 build FAILED\n";
            }
        }

        // PHOBIC + perfect_filter<16>
        {
            auto t0 = high_resolution_clock::now();
            auto phf = phobic5::builder{}.add_all(keys).build();
            if (phf) {
                auto pf = perfect_filter<phobic5, 16>::build(std::move(*phf), keys);
                auto t1 = high_resolution_clock::now();
                auto rng_copy = rng;
                auto [med, p99] = measure_query([&](size_t) {
                    volatile auto s = pf.slot_for(keys[kd(rng_copy)]); (void)s;
                }, qi);
                double bpk = pf.phf().bits_per_key() + 16.0;
                print_result({"phobic5+pf16", keys.size(),
                    duration_cast<microseconds>(t1 - t0).count() / 1000.0,
                    med, p99, bpk, pf.phf().memory_bytes() + keys.size() * 2});
            }
        }

        // RecSplit (existing, with baked-in 64-bit fingerprints)
        {
            auto t0 = high_resolution_clock::now();
            auto h = recsplit8::builder{}.add_all(keys).build();
            auto t1 = high_resolution_clock::now();
            if (h) {
                auto rng_copy = rng;
                auto [med, p99] = measure_query([&](size_t) {
                    volatile auto s = h->slot_for(keys[kd(rng_copy)]); (void)s;
                }, qi);
                print_result({"recsplit8", keys.size(),
                    duration_cast<microseconds>(t1 - t0).count() / 1000.0,
                    med, p99, h->bits_per_key(), h->memory_bytes()});
            }
        }

        // CHD (existing)
        {
            auto t0 = high_resolution_clock::now();
            auto h = chd_hasher::builder{}.add_all(keys).build();
            auto t1 = high_resolution_clock::now();
            if (h) {
                auto rng_copy = rng;
                auto [med, p99] = measure_query([&](size_t) {
                    volatile auto s = h->slot_for(keys[kd(rng_copy)]); (void)s;
                }, qi);
                print_result({"chd", keys.size(),
                    duration_cast<microseconds>(t1 - t0).count() / 1000.0,
                    med, p99, h->bits_per_key(), h->memory_bytes()});
            }
        }

        // BBHash (existing)
        {
            auto t0 = high_resolution_clock::now();
            auto h = bbhash3::builder{}.add_all(keys).build();
            auto t1 = high_resolution_clock::now();
            if (h) {
                auto rng_copy = rng;
                auto [med, p99] = measure_query([&](size_t) {
                    volatile auto s = h->slot_for(keys[kd(rng_copy)]); (void)s;
                }, qi);
                print_result({"bbhash3", keys.size(),
                    duration_cast<microseconds>(t1 - t0).count() / 1000.0,
                    med, p99, h->bits_per_key(), h->memory_bytes()});
            }
        }
    }

    return 0;
}
