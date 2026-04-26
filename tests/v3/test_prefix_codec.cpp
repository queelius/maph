/**
 * @file test_prefix_codec.cpp
 * @brief Tests for prefix_codec + non-member distribution measurement.
 *
 * The static tests check the codec's contract: prefix-free assignment,
 * encode/decode round-trip, codespace shares, Kraft validation.
 *
 * The empirical test measures the relationship between stored-value
 * frequency distribution and non-member query distribution under
 * encoded_retrieval<ribbon_retrieval<M>, prefix_codec<V, M>>. This
 * is the cipher-map "codespace dominates" claim made concrete.
 */

#include <catch2/catch_test_macros.hpp>

#include <maph/codecs/prefix_codec.hpp>
#include <maph/concepts/codec.hpp>
#include <maph/retrieval/encoded_retrieval.hpp>
#include <maph/retrieval/ribbon_retrieval.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

using namespace maph;

namespace {

enum class V : uint8_t { A, B, C, D };

std::vector<std::string> make_keys(size_t n, uint64_t seed = 42) {
    std::vector<std::string> keys;
    keys.reserve(n);
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<int> b(0, 255);
    for (size_t i = 0; i < n; ++i) {
        std::string k(16, '\0');
        for (auto& c : k) c = static_cast<char>(b(rng));
        keys.push_back(std::move(k));
    }
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    return keys;
}

}  // namespace

// ===== Static contract: codec interface =====

TEST_CASE("prefix_codec satisfies codec concept", "[prefix_codec][concept]") {
    STATIC_REQUIRE(codec<prefix_codec<V, 4>>);
    STATIC_REQUIRE(codec<prefix_codec<V, 8>>);
}

TEST_CASE("prefix_codec: complete code (Kraft = 1)", "[prefix_codec]") {
    // {A: 1 bit, B: 2 bits, C: 2 bits} - Kraft = 1/2 + 1/4 + 1/4 = 1
    prefix_codec<V, 4> c({{V::A, 1}, {V::B, 2}, {V::C, 2}}, V::D);

    // Round-trip: encode then decode returns the original.
    REQUIRE(c.decode(c.encode(V::A)) == V::A);
    REQUIRE(c.decode(c.encode(V::B)) == V::B);
    REQUIRE(c.decode(c.encode(V::C)) == V::C);

    // Class sizes match prefix lengths.
    REQUIRE(c.codespace_share(V::A) == 0.5);
    REQUIRE(c.codespace_share(V::B) == 0.25);
    REQUIRE(c.codespace_share(V::C) == 0.25);
    // No surplus (Kraft = 1).
    REQUIRE(c.codespace_share(V::D) == 0.0);

    // All 16 patterns should decode to A, B, or C.
    std::array<int, 4> counts{0, 0, 0, 0};
    for (uint64_t p = 0; p < 16; ++p) {
        counts[static_cast<size_t>(c.decode(p))]++;
    }
    REQUIRE(counts[static_cast<size_t>(V::A)] == 8);  // class size 8 of 16 = 0.5
    REQUIRE(counts[static_cast<size_t>(V::B)] == 4);  // 4 of 16 = 0.25
    REQUIRE(counts[static_cast<size_t>(V::C)] == 4);
    REQUIRE(counts[static_cast<size_t>(V::D)] == 0);
}

TEST_CASE("prefix_codec: incomplete code routes surplus to default", "[prefix_codec]") {
    // {A: 2, B: 2, C: 2} - Kraft = 3/4 < 1, surplus = 1/4
    prefix_codec<V, 4> c({{V::A, 2}, {V::B, 2}, {V::C, 2}}, V::D);
    REQUIRE(c.codespace_share(V::A) == 0.25);
    REQUIRE(c.codespace_share(V::B) == 0.25);
    REQUIRE(c.codespace_share(V::C) == 0.25);
    REQUIRE(c.codespace_share(V::D) == 0.25);  // surplus

    std::array<int, 4> counts{0, 0, 0, 0};
    for (uint64_t p = 0; p < 16; ++p) {
        counts[static_cast<size_t>(c.decode(p))]++;
    }
    REQUIRE(counts[static_cast<size_t>(V::A)] == 4);
    REQUIRE(counts[static_cast<size_t>(V::B)] == 4);
    REQUIRE(counts[static_cast<size_t>(V::C)] == 4);
    REQUIRE(counts[static_cast<size_t>(V::D)] == 4);
}

TEST_CASE("prefix_codec: Kraft inequality is enforced", "[prefix_codec]") {
    // {A: 1, B: 1} - Kraft = 1.0 + 0 = 1, OK
    REQUIRE_NOTHROW((prefix_codec<V, 4>{{{V::A, 1}, {V::B, 1}}, V::C}));
    // {A: 1, B: 1, C: 1} - Kraft = 1.5 > 1, illegal
    REQUIRE_THROWS_AS((prefix_codec<V, 4>{{{V::A, 1}, {V::B, 1}, {V::C, 1}}, V::D}),
                      std::invalid_argument);
}

TEST_CASE("prefix_codec: from_frequencies builds a Huffman code", "[prefix_codec]") {
    // Skewed: A is dominant.
    auto c = prefix_codec<V, 8>::from_frequencies(
        {{V::A, 0.90}, {V::B, 0.05}, {V::C, 0.05}}, V::D);

    // A should have a shorter codeword than B or C.
    auto e_a = c.entries();
    auto find_len = [&](V v) -> unsigned {
        for (const auto& e : e_a) if (e.value == v) return e.length;
        return 0;
    };
    unsigned la = find_len(V::A), lb = find_len(V::B), lc = find_len(V::C);
    REQUIRE(la <= lb);
    REQUIRE(la <= lc);
    // Round-trip works for each.
    REQUIRE(c.decode(c.encode(V::A)) == V::A);
    REQUIRE(c.decode(c.encode(V::B)) == V::B);
    REQUIRE(c.decode(c.encode(V::C)) == V::C);
}

// ===== encoded_retrieval<ribbon, prefix_codec> round-trip =====

TEST_CASE("encoded_retrieval<ribbon, prefix_codec>: keys in S round-trip",
          "[prefix_codec][integration]") {
    auto keys = make_keys(500);
    std::vector<V> values;
    values.reserve(keys.size());
    std::mt19937_64 rng{7};
    std::uniform_int_distribution<int> pick(0, 2);
    for (size_t i = 0; i < keys.size(); ++i) {
        values.push_back(static_cast<V>(pick(rng)));
    }

    prefix_codec<V, 4> c({{V::A, 1}, {V::B, 2}, {V::C, 2}}, V::D);
    using Enc = encoded_retrieval<ribbon_retrieval<4>, prefix_codec<V, 4>>;
    auto built = Enc::builder(c).add_all(keys, values).build();
    REQUIRE(built.has_value());

    for (size_t i = 0; i < keys.size(); ++i) {
        REQUIRE(built->lookup(keys[i]) == values[i]);
    }
}

// ===== Empirical claim: non-member distribution =====
//
// The cipher-map framing claims that for a skewed value distribution,
// non-member queries reproduce the skew. The mechanism via prefix_codec
// is: stored patterns concentrate within frequent values' codespace,
// so ribbon's solution_ also concentrates there, and XOR of solution_
// entries has the same distribution as the (frequency-weighted) stored
// patterns.

TEST_CASE("non-member distribution matches codec codespace (not stored frequency)",
          "[prefix_codec][nonmember]") {
    // KEY EMPIRICAL FINDING: ribbon's solution entries become approximately
    // uniform random over GF(2)^M after Gaussian elimination of a sparse
    // system. So non-member XORs are approximately uniform M-bit patterns.
    // What controls the decoded distribution is therefore the CODEC, not
    // the stored-value frequencies.
    //
    // Stored values 90% A / 5% B / 5% C,
    // Codec with prefix lengths {A: 1, B: 2, C: 2} (Kraft = 1):
    //   A's class fraction = 0.5
    //   B's class fraction = 0.25
    //   C's class fraction = 0.25
    //
    // Predicted non-member fractions: 0.50 / 0.25 / 0.25, NOT 0.90 / 0.05 / 0.05.

    auto keys = make_keys(2000);
    std::vector<V> values;
    values.reserve(keys.size());
    std::mt19937_64 rng{42};
    for (size_t i = 0; i < keys.size(); ++i) {
        double r = std::uniform_real_distribution<double>{}(rng);
        if (r < 0.90) values.push_back(V::A);
        else if (r < 0.95) values.push_back(V::B);
        else values.push_back(V::C);
    }
    // Even though we use Huffman lengths derived from frequencies, the
    // resulting class fractions are powers of 2 (0.5 / 0.25 / 0.25), not
    // the raw 0.9 / 0.05 / 0.05.
    prefix_codec<V, 4> cdc({{V::A, 1}, {V::B, 2}, {V::C, 2}}, V::D);

    using Enc = encoded_retrieval<ribbon_retrieval<4>, prefix_codec<V, 4>>;
    auto built = Enc::builder(cdc).add_all(keys, values).build();
    REQUIRE(built.has_value());

    // In-S round-trip.
    for (size_t i = 0; i < keys.size(); ++i) {
        REQUIRE(built->lookup(keys[i]) == values[i]);
    }

    // Non-member distribution.
    std::array<size_t, 4> counts{0, 0, 0, 0};
    const size_t N_unknown = 30000;
    for (size_t i = 0; i < N_unknown; ++i) {
        std::string u = "UNKNOWN_KEY_" + std::to_string(i);
        counts[static_cast<size_t>(built->lookup(u))]++;
    }
    double frac_a = double(counts[0]) / N_unknown;
    double frac_b = double(counts[1]) / N_unknown;
    double frac_c = double(counts[2]) / N_unknown;
    double frac_d = double(counts[3]) / N_unknown;

    // Each fraction within +/- 3% of the codespace share. With 30K
    // samples and a ~uniform underlying distribution, sample stddev
    // is sqrt(p(1-p)/N) ~= 0.003, so 3% is generous.
    REQUIRE(std::abs(frac_a - 0.50) < 0.03);
    REQUIRE(std::abs(frac_b - 0.25) < 0.03);
    REQUIRE(std::abs(frac_c - 0.25) < 0.03);
    REQUIRE(frac_d < 0.005);  // no surplus in this codec

    // Cipher-map design implication: codec controls non-member behavior.
    // To bias non-members toward a default value V, give V a larger
    // codespace class via shorter prefix; the stored-value distribution
    // itself does NOT determine non-member output.
}

TEST_CASE("non-member distribution: same codec, different storage frequencies",
          "[prefix_codec][nonmember][contrastive]") {
    // Direct test of the claim that non-member output depends on the
    // codec, not the stored-value frequencies. We build two structures
    // with the SAME codec but radically different value distributions,
    // and verify the non-member distributions match each other (and
    // the codec's class shares).

    auto keys_a = make_keys(2000, /*seed=*/1);
    auto keys_b = make_keys(2000, /*seed=*/2);
    REQUIRE(keys_a.size() == keys_b.size());

    // Storage 1: 99% A, 0.5% B, 0.5% C.
    std::vector<V> values_a;
    values_a.reserve(keys_a.size());
    std::mt19937_64 rng_a{11};
    for (size_t i = 0; i < keys_a.size(); ++i) {
        double r = std::uniform_real_distribution<double>{}(rng_a);
        if (r < 0.99) values_a.push_back(V::A);
        else if (r < 0.995) values_a.push_back(V::B);
        else values_a.push_back(V::C);
    }

    // Storage 2: 50% A, 25% B, 25% C.
    std::vector<V> values_b;
    values_b.reserve(keys_b.size());
    std::mt19937_64 rng_b{22};
    for (size_t i = 0; i < keys_b.size(); ++i) {
        double r = std::uniform_real_distribution<double>{}(rng_b);
        if (r < 0.50) values_b.push_back(V::A);
        else if (r < 0.75) values_b.push_back(V::B);
        else values_b.push_back(V::C);
    }

    // Identical codec for both.
    prefix_codec<V, 4> cdc({{V::A, 1}, {V::B, 2}, {V::C, 2}}, V::D);

    using Enc = encoded_retrieval<ribbon_retrieval<4>, prefix_codec<V, 4>>;
    auto built_a = Enc::builder(cdc).add_all(keys_a, values_a).build();
    auto built_b = Enc::builder(cdc).add_all(keys_b, values_b).build();
    REQUIRE(built_a.has_value());
    REQUIRE(built_b.has_value());

    // Both should round-trip on their own keys (definitional).
    for (size_t i = 0; i < keys_a.size(); ++i)
        REQUIRE(built_a->lookup(keys_a[i]) == values_a[i]);
    for (size_t i = 0; i < keys_b.size(); ++i)
        REQUIRE(built_b->lookup(keys_b[i]) == values_b[i]);

    // Non-member distributions should be approximately equal despite the
    // wildly different storage distributions.
    auto measure = [](const Enc& e, const std::string& prefix) {
        std::array<size_t, 4> counts{0, 0, 0, 0};
        const size_t N = 30000;
        for (size_t i = 0; i < N; ++i) {
            counts[static_cast<size_t>(e.lookup(prefix + std::to_string(i)))]++;
        }
        std::array<double, 4> fracs;
        for (size_t i = 0; i < 4; ++i) fracs[i] = double(counts[i]) / N;
        return fracs;
    };
    auto fa = measure(*built_a, "UNK_A_");
    auto fb = measure(*built_b, "UNK_B_");

    // Each fraction within 3% of the codec's codespace share (0.5/0.25/0.25).
    REQUIRE(std::abs(fa[0] - 0.50) < 0.03);
    REQUIRE(std::abs(fa[1] - 0.25) < 0.03);
    REQUIRE(std::abs(fa[2] - 0.25) < 0.03);
    REQUIRE(std::abs(fb[0] - 0.50) < 0.03);
    REQUIRE(std::abs(fb[1] - 0.25) < 0.03);
    REQUIRE(std::abs(fb[2] - 0.25) < 0.03);

    // And they should be close to each other (within ~5% per category).
    REQUIRE(std::abs(fa[0] - fb[0]) < 0.05);
    REQUIRE(std::abs(fa[1] - fb[1]) < 0.05);
    REQUIRE(std::abs(fa[2] - fb[2]) < 0.05);
}

TEST_CASE("encode_random spreads stored patterns across class",
          "[prefix_codec][encode_random]") {
    // With encode_random, the stored patterns for a value should span
    // its codespace class, not collapse to a canonical pattern.
    prefix_codec<V, 8> c({{V::A, 1}, {V::B, 2}, {V::C, 2}}, V::D);
    std::mt19937_64 rng{2026};

    std::array<int, 256> hits{};
    for (int i = 0; i < 10000; ++i) {
        uint64_t p = c.encode_random(V::A, rng);
        REQUIRE(p < 256);
        hits[p]++;
    }

    // A's class is the top half of the codespace (128 patterns).
    int hit_count = 0;
    for (int p = 0; p < 128; ++p) if (hits[p] > 0) ++hit_count;
    int wrong_count = 0;
    for (int p = 128; p < 256; ++p) wrong_count += hits[p];

    // Should hit most of A's class with 10K samples.
    REQUIRE(hit_count > 100);
    REQUIRE(wrong_count == 0);
}
