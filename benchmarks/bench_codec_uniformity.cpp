/**
 * @file bench_codec_uniformity.cpp
 * @brief Measure how stored-value diversity affects codec-controlled
 *        non-member distribution.
 *
 * The claim from CODESPACE_NONMEMBERS.md is that
 *   P(lookup(k_unknown) = v) ~= |class(v)| / 2^M
 * for ribbon-based encoded_retrieval. The justification rests on
 * ribbon's solution_ entries being approximately uniform random in
 * GF(2)^M after Gaussian elimination of a sparse system. That property
 * holds when stored values span the codespace; degenerate cases (all
 * keys store the same value) reduce the system to triviality and the
 * non-member distribution collapses.
 *
 * This program quantifies the transition. For each (M, codec, storage
 * diversity) configuration:
 *   1. Build encoded_retrieval<ribbon_retrieval<M>, prefix_codec<V, M>>
 *   2. Query a large fixed set of non-members.
 *   3. Compare observed value-frequency distribution to codec-predicted.
 *   4. Report KL divergence and total variation distance.
 *
 * Usage:
 *   bench_codec_uniformity                      # default sweep
 *   bench_codec_uniformity --keys=5000 --queries=50000
 *
 * Output is one row per (M, codec, diversity_label) with deviations.
 */

#include "bench_harness.hpp"

#include <maph/codecs/prefix_codec.hpp>
#include <maph/retrieval/encoded_retrieval.hpp>
#include <maph/retrieval/ribbon_retrieval.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace maph;
using namespace maph::bench;

namespace {

// 16-value alphabet; choose codec width M up to 8 to give room.
enum class V : uint8_t {
    V0=0, V1, V2, V3, V4, V5, V6, V7, V8, V9, V10, V11, V12, V13, V14, V15
};

constexpr size_t ALPHABET = 16;

double kl_divergence(const std::vector<double>& observed,
                     const std::vector<double>& predicted) {
    double kl = 0.0;
    for (size_t i = 0; i < observed.size(); ++i) {
        double p = observed[i];
        double q = predicted[i];
        if (p > 0 && q > 0) {
            kl += p * std::log(p / q);
        }
    }
    return kl;
}

double total_variation(const std::vector<double>& a,
                       const std::vector<double>& b) {
    double tv = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        tv += std::abs(a[i] - b[i]);
    }
    return 0.5 * tv;
}

struct experiment_result {
    std::string label;
    unsigned M;
    size_t distinct_stored;
    double kl;
    double tv;
    bool ok;
    std::vector<double> observed;
    std::vector<double> predicted;
};

template <unsigned M>
experiment_result run_one(
    const std::string& label,
    const std::vector<std::pair<V, unsigned>>& code_lengths,
    const std::vector<V>& alphabet,
    std::vector<V>& stored_values,
    const std::vector<std::string>& keys,
    size_t n_unknowns)
{
    experiment_result r{};
    r.label = label;
    r.M = M;
    r.ok = false;

    // Count distinct stored values (informational).
    {
        std::array<bool, ALPHABET> seen{};
        for (V v : stored_values) seen[size_t(v)] = true;
        size_t d = 0;
        for (bool b : seen) if (b) ++d;
        r.distinct_stored = d;
    }

    // Build codec. Use the supplied (value, length) pairs; default = V0
    // arbitrarily (won't matter if Kraft = 1).
    prefix_codec<V, M> codec(code_lengths, V::V0);

    // Build encoded retrieval.
    using Enc = encoded_retrieval<ribbon_retrieval<M>, prefix_codec<V, M>>;
    auto built = typename Enc::builder(codec)
        .add_all(std::span<const std::string>{keys},
                 std::span<const V>{stored_values})
        .build();
    if (!built.has_value()) return r;

    // Predicted distribution: codec codespace shares.
    std::vector<double> predicted(ALPHABET, 0.0);
    for (V v : alphabet) {
        predicted[size_t(v)] = codec.codespace_share(v);
    }

    // Measure non-member distribution.
    std::vector<size_t> obs_counts(ALPHABET, 0);
    for (size_t i = 0; i < n_unknowns; ++i) {
        std::string u = "UNKNOWN_" + std::to_string(i);
        obs_counts[size_t(built->lookup(u))]++;
    }
    std::vector<double> observed(ALPHABET, 0.0);
    for (size_t i = 0; i < ALPHABET; ++i) {
        observed[i] = double(obs_counts[i]) / double(n_unknowns);
    }

    r.kl = kl_divergence(observed, predicted);
    r.tv = total_variation(observed, predicted);
    r.observed = observed;
    r.predicted = predicted;
    r.ok = true;
    return r;
}

void print_header() {
    std::cout << std::left
        << std::setw(40) << "configuration"
        << std::right
        << std::setw(4)  << "M"
        << std::setw(10) << "stored_d"
        << std::setw(12) << "KL_div"
        << std::setw(12) << "TV_dist"
        << std::setw(7)  << "ok"
        << '\n';
}

void print_row(const experiment_result& r) {
    std::cout << std::left
        << std::setw(40) << r.label
        << std::right << std::fixed
        << std::setw(4)  << r.M
        << std::setw(10) << r.distinct_stored
        << std::setw(12) << std::setprecision(5) << r.kl
        << std::setw(12) << std::setprecision(5) << r.tv
        << std::setw(7)  << (r.ok ? "1" : "0")
        << '\n';
    std::cout.flush();
}

std::vector<std::string> gen_keys(size_t n, uint64_t seed = 42) {
    std::vector<std::string> out;
    out.reserve(n);
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<int> b(0, 255);
    for (size_t i = 0; i < n; ++i) {
        std::string k(16, '\0');
        for (auto& c : k) c = char(b(rng));
        out.push_back(std::move(k));
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// Build storage with exactly k distinct values, each used n/k times,
// drawn from a contiguous prefix of the alphabet.
std::vector<V> storage_uniform_k_values(size_t n, size_t k) {
    std::vector<V> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) out.push_back(V(i % k));
    return out;
}

// Skewed: 95% value 0, 5% spread across (k-1) others.
std::vector<V> storage_skewed_to_v0(size_t n, size_t k) {
    std::vector<V> out;
    out.reserve(n);
    std::mt19937_64 rng{99};
    std::uniform_real_distribution<double> u(0.0, 1.0);
    for (size_t i = 0; i < n; ++i) {
        if (u(rng) < 0.95) out.push_back(V::V0);
        else out.push_back(V(1 + (i % std::max<size_t>(1, k - 1))));
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    cli_args args(argc, argv);
    size_t n_keys = args.get_size("keys", 5'000);
    size_t n_unknowns = args.get_size("queries", 50'000);

    std::cerr << "bench_codec_uniformity\n"
              << "  S size: " << n_keys << "\n"
              << "  unknown queries per config: " << n_unknowns << "\n\n";

    auto keys = gen_keys(n_keys);

    print_header();

    // ── Sweep 1: M = 4, balanced codec, vary stored diversity. ────────────
    // codec: 4 values with code lengths 2,2,2,2 (Kraft = 1, equal shares).
    // Distinct stored values: 1, 2, 3, 4.
    {
        std::vector<std::pair<V, unsigned>> code_lengths = {
            {V::V0, 2}, {V::V1, 2}, {V::V2, 2}, {V::V3, 2}
        };
        std::vector<V> alphabet = {V::V0, V::V1, V::V2, V::V3};
        for (size_t k : {1, 2, 3, 4}) {
            auto stored = storage_uniform_k_values(keys.size(), k);
            auto r = run_one<4>(
                "M=4 balanced, k_distinct=" + std::to_string(k),
                code_lengths, alphabet, stored, keys, n_unknowns);
            print_row(r);
        }
        std::cout << '\n';
    }

    // ── Sweep 2: M = 4, skewed codec (50/25/25/0% with default), vary k. ─
    // Codec assigns A->50%, B->25%, C->25% codespace; D is default (0%).
    // This is the prefix_codec test setup; vary how diverse storage is.
    {
        std::vector<std::pair<V, unsigned>> code_lengths = {
            {V::V0, 1}, {V::V1, 2}, {V::V2, 2}
        };
        std::vector<V> alphabet = {V::V0, V::V1, V::V2, V::V3};
        for (size_t k : {1, 2, 3}) {
            auto stored = storage_uniform_k_values(keys.size(), k);
            auto r = run_one<4>(
                "M=4 skewed, k_distinct=" + std::to_string(k),
                code_lengths, alphabet, stored, keys, n_unknowns);
            print_row(r);
        }
        std::cout << '\n';
    }

    // ── Sweep 3: M = 8, balanced codec, larger alphabet. ──────────────────
    // 8 values at code length 3 each (Kraft = 1).
    {
        std::vector<std::pair<V, unsigned>> code_lengths = {
            {V::V0, 3}, {V::V1, 3}, {V::V2, 3}, {V::V3, 3},
            {V::V4, 3}, {V::V5, 3}, {V::V6, 3}, {V::V7, 3}
        };
        std::vector<V> alphabet = {V::V0, V::V1, V::V2, V::V3,
                                    V::V4, V::V5, V::V6, V::V7};
        for (size_t k : {1, 2, 4, 8}) {
            auto stored = storage_uniform_k_values(keys.size(), k);
            auto r = run_one<8>(
                "M=8 balanced, k_distinct=" + std::to_string(k),
                code_lengths, alphabet, stored, keys, n_unknowns);
            print_row(r);
        }
        std::cout << '\n';
    }

    // ── Sweep 4: M = 4, skewed CODEC + heavily skewed storage. ────────────
    // Tests whether storage skew (95% V0) interacts with codec skew.
    {
        std::vector<std::pair<V, unsigned>> code_lengths = {
            {V::V0, 1}, {V::V1, 2}, {V::V2, 2}
        };
        std::vector<V> alphabet = {V::V0, V::V1, V::V2, V::V3};
        for (size_t k : {1, 2, 3}) {
            auto stored = storage_skewed_to_v0(keys.size(), k);
            auto r = run_one<4>(
                "M=4 skewed_codec + skewed_storage, k=" + std::to_string(k),
                code_lengths, alphabet, stored, keys, n_unknowns);
            print_row(r);
        }
        std::cout << '\n';
    }

    return 0;
}
