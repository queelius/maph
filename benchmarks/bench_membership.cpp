/**
 * @file bench_membership.cpp
 * @brief Comparative benchmark for membership verification strategies
 */

#include <maph/core.hpp>
#include <maph/hashers_perfect.hpp>
#include <maph/membership.hpp>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <random>
#include <algorithm>
#include <numeric>
#include <vector>
#include <string>
#include <functional>

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

std::vector<std::string> gen_unknowns(size_t count, uint64_t seed = 99999) {
    std::vector<std::string> keys;
    keys.reserve(count);
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<int> byte_dist(0, 255);
    for (size_t i = 0; i < count; ++i) {
        std::string key = "UNK_";
        for (size_t j = 0; j < 16; ++j) key += static_cast<char>(byte_dist(rng));
        keys.push_back(std::move(key));
    }
    return keys;
}

struct latency_stats {
    double median_ns, p99_ns, avg_ns;
};

template<typename Fn>
latency_stats measure(Fn&& fn, size_t iters) {
    std::vector<double> times;
    times.reserve(iters);
    for (size_t i = 0; i < iters; ++i) {
        auto t0 = high_resolution_clock::now();
        fn(i);
        auto t1 = high_resolution_clock::now();
        times.push_back(static_cast<double>(duration_cast<nanoseconds>(t1 - t0).count()));
    }
    std::sort(times.begin(), times.end());
    return {
        times[times.size() / 2],
        times[static_cast<size_t>(times.size() * 0.99)],
        std::accumulate(times.begin(), times.end(), 0.0) / static_cast<double>(times.size())
    };
}

struct bench_result {
    std::string strategy;
    unsigned fp_bits;
    size_t key_count;
    double bits_per_key;
    double build_ms;
    double q_known_med_ns, q_known_p99_ns;
    double q_unk_med_ns, q_unk_p99_ns;
    double fp_rate;
    size_t mem_bytes;
};

void print_header() {
    std::cout << "strategy\tfp_bits\tkeys\tbits_per_key\tbuild_ms\t"
              << "q_known_med_ns\tq_known_p99_ns\tq_unk_med_ns\tq_unk_p99_ns\t"
              << "fp_rate\tmem_kb\n";
}

void print_result(const bench_result& r) {
    std::cout << std::fixed
              << r.strategy << '\t' << r.fp_bits << '\t' << r.key_count << '\t'
              << std::setprecision(2) << r.bits_per_key << '\t' << r.build_ms << '\t'
              << r.q_known_med_ns << '\t' << r.q_known_p99_ns << '\t'
              << r.q_unk_med_ns << '\t' << r.q_unk_p99_ns << '\t'
              << std::setprecision(10) << r.fp_rate << '\t'
              << std::setprecision(1) << (r.mem_bytes / 1024.0) << '\n';
}

template<unsigned FPBits>
bench_result run_packed(const std::vector<std::string>& keys,
                  const std::vector<std::string>& unknowns,
                  recsplit8& hasher, size_t qi) {
    auto slot_fn = [&](std::string_view k) -> std::optional<size_t> {
        auto s = hasher.slot_for(k);
        return s ? std::optional<size_t>{s->value} : std::nullopt;
    };
    packed_fingerprint_array<FPBits> pfa;
    auto t0 = high_resolution_clock::now();
    pfa.build(keys, slot_fn, keys.size());
    auto t1 = high_resolution_clock::now();

    std::mt19937_64 rng{123};
    std::uniform_int_distribution<size_t> kd(0, keys.size() - 1);
    std::uniform_int_distribution<size_t> ud(0, unknowns.size() - 1);

    auto ks = measure([&](size_t) {
        auto& k = keys[kd(rng)]; auto s = slot_fn(k);
        volatile bool v = pfa.verify(k, *s); (void)v;
    }, qi);
    rng.seed(456);
    auto us = measure([&](size_t) {
        auto& k = unknowns[ud(rng)];
        volatile bool v = pfa.verify(k, membership_fingerprint(k) % keys.size()); (void)v;
    }, qi);

    size_t fps = 0;
    size_t fp_trials = std::min(unknowns.size(), size_t{1000000});
    for (size_t i = 0; i < fp_trials; ++i) {
        if (pfa.verify(unknowns[i], membership_fingerprint(unknowns[i]) % keys.size())) ++fps;
    }

    return {"packed", FPBits, keys.size(), pfa.bits_per_key(keys.size()),
            duration_cast<microseconds>(t1 - t0).count() / 1000.0,
            ks.median_ns, ks.p99_ns, us.median_ns, us.p99_ns,
            static_cast<double>(fps) / fp_trials, pfa.memory_bytes()};
}

int main(int argc, char** argv) {
    std::vector<size_t> key_counts = {1000000};
    size_t qi = 1000000;

    if (argc > 1) {
        key_counts.clear();
        for (int i = 1; i < argc; ++i) key_counts.push_back(std::stoul(argv[i]));
    }

    std::cerr << "Membership Verification Strategy Benchmark\n\n";
    print_header();

    for (size_t kc : key_counts) {
        auto keys = gen_keys(kc);
        auto unknowns = gen_unknowns(std::min(kc, size_t{1000000}));
        std::cerr << "=== " << keys.size() << " keys ===\n";

        std::cerr << "Building RecSplit..." << std::flush;
        auto hr = recsplit8::builder{}.add_all(keys).with_threads(4).build();
        if (!hr) { std::cerr << " FAILED, skipping\n"; continue; }
        auto hasher = std::move(hr.value());
        std::cerr << " done\n";

        // packed: 8, 16, 32
        print_result(run_packed<8>(keys, unknowns, hasher, qi));
        print_result(run_packed<16>(keys, unknowns, hasher, qi));
        print_result(run_packed<32>(keys, unknowns, hasher, qi));
    }

    return 0;
}
