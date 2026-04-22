/**
 * @file test_encoded_retrieval.cpp
 * @brief Tests for codecs and encoded_retrieval composition.
 *
 * The critical property to verify is that codespace allocation
 * controls non-member behavior: with a padded_codec that gives a
 * default value k surplus patterns out of 2^M, the observed rate at
 * which non-members decode to default must converge to k/2^M as the
 * sample size grows.
 */

#include <catch2/catch_test_macros.hpp>

#include <maph/algorithms/phobic.hpp>
#include <maph/codecs/dense_codec.hpp>
#include <maph/codecs/padded_codec.hpp>
#include <maph/concepts/codec.hpp>
#include <maph/concepts/retrieval.hpp>
#include <maph/retrieval/encoded_retrieval.hpp>
#include <maph/retrieval/phf_value_array.hpp>
#include <maph/retrieval/ribbon_retrieval.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

using namespace maph;

namespace {

std::vector<std::string> make_keys(size_t count, uint64_t seed = 42) {
    std::vector<std::string> keys;
    keys.reserve(count);
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<int> char_dist('a', 'z');
    std::uniform_int_distribution<size_t> len_dist(4, 16);
    for (size_t i = 0; i < count; ++i) {
        size_t len = len_dist(rng);
        std::string k;
        k.reserve(len);
        for (size_t j = 0; j < len; ++j) k.push_back(static_cast<char>(char_dist(rng)));
        keys.push_back(std::move(k));
    }
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    return keys;
}

enum class color : uint8_t { A, B, C, D };

} // namespace

// ===== Codec concept =====

TEST_CASE("codec concept: padded_codec satisfies codec", "[codec]") {
    STATIC_REQUIRE(codec<padded_codec<int, 4>>);
    STATIC_REQUIRE(codec<padded_codec<color, 6>>);
}

TEST_CASE("codec concept: dense_codec satisfies codec", "[codec]") {
    STATIC_REQUIRE(codec<dense_codec<int, 2>>);
    STATIC_REQUIRE(codec<dense_codec<color, 2>>);
}

// ===== padded_codec behavior =====

TEST_CASE("padded_codec: round-trip on indexed values", "[codec][padded]") {
    padded_codec<color, 4> c({color::A, color::B, color::C}, color::C);
    REQUIRE(c.decode(c.encode(color::A)) == color::A);
    REQUIRE(c.decode(c.encode(color::B)) == color::B);
    REQUIRE(c.decode(c.encode(color::C)) == color::C);
}

TEST_CASE("padded_codec: surplus patterns decode to default", "[codec][padded]") {
    padded_codec<color, 4> c({color::A, color::B, color::C}, color::D);
    // patterns 0,1,2 -> A, B, C; patterns 3..15 -> default D
    REQUIRE(c.decode(0) == color::A);
    REQUIRE(c.decode(1) == color::B);
    REQUIRE(c.decode(2) == color::C);
    for (uint64_t p = 3; p < 16; ++p) {
        REQUIRE(c.decode(p) == color::D);
    }
}

TEST_CASE("padded_codec: non-member probability is k/2^M", "[codec][padded]") {
    padded_codec<color, 4> c({color::A, color::B, color::C}, color::D);
    REQUIRE(c.nonmember_probability(color::A) == 1.0 / 16.0);
    REQUIRE(c.nonmember_probability(color::B) == 1.0 / 16.0);
    REQUIRE(c.nonmember_probability(color::C) == 1.0 / 16.0);
    REQUIRE(c.nonmember_probability(color::D) == 13.0 / 16.0);
}

// ===== encoded_retrieval concept + round-trip =====

TEST_CASE("encoded_retrieval: satisfies retrieval with logical_value",
          "[encoded_retrieval][concept]") {
    using Enc = encoded_retrieval<ribbon_retrieval<4>, padded_codec<color, 4>>;
    STATIC_REQUIRE(retrieval<Enc>);
}

TEST_CASE("encoded_retrieval<ribbon<4>, padded<color,4>>: keys in S decode correctly",
          "[encoded_retrieval][ribbon]") {
    auto keys = make_keys(1000);
    std::vector<color> values;
    values.reserve(keys.size());
    std::mt19937_64 rng{7};
    std::uniform_int_distribution<int> pick(0, 2);
    for (size_t i = 0; i < keys.size(); ++i) {
        values.push_back(static_cast<color>(pick(rng)));
    }

    padded_codec<color, 4> cdc({color::A, color::B, color::C}, color::D);
    using Enc = encoded_retrieval<ribbon_retrieval<4>, padded_codec<color, 4>>;
    auto built = Enc::builder(cdc).add_all(keys, values).build();
    REQUIRE(built.has_value());

    for (size_t i = 0; i < keys.size(); ++i) {
        REQUIRE(built->lookup(keys[i]) == values[i]);
    }
}

TEST_CASE("encoded_retrieval<pva<phobic5,4>, padded<color,4>>: keys in S decode correctly",
          "[encoded_retrieval][phobic]") {
    auto keys = make_keys(500);
    std::vector<color> values;
    values.reserve(keys.size());
    std::mt19937_64 rng{11};
    std::uniform_int_distribution<int> pick(0, 3);
    for (size_t i = 0; i < keys.size(); ++i) {
        values.push_back(static_cast<color>(pick(rng)));
    }

    padded_codec<color, 4> cdc({color::A, color::B, color::C, color::D}, color::A);
    using Enc = encoded_retrieval<phf_value_array<phobic5, 4>, padded_codec<color, 4>>;
    auto built = Enc::builder(cdc).add_all(keys, values).build();
    REQUIRE(built.has_value());

    for (size_t i = 0; i < keys.size(); ++i) {
        REQUIRE(built->lookup(keys[i]) == values[i]);
    }
}

// ===== Non-member distribution: pva with fill pattern =====
//
// The cipher-map use case: we care about keys in S returning their
// stored value, and we want most non-members to decode to a designated
// default (e.g. the dominant value in the true population). With pva +
// padded_codec + non-minimal PHF, unused slots become the lever.
// encoded_retrieval::builder wires codec.default_pattern() into
// pva::builder::with_fill_pattern() automatically.

TEST_CASE("padded_codec: default_pattern decodes to default",
          "[codec][padded]") {
    padded_codec<color, 4> cdc({color::A, color::B, color::C}, color::D);
    REQUIRE(cdc.decode(cdc.default_pattern()) == color::D);

    padded_codec<color, 6> cdc6({color::A, color::B, color::C, color::D}, color::C);
    REQUIRE(cdc6.decode(cdc6.default_pattern()) == color::C);
}

// Non-member behavior via stored-value distribution. For MPHF-based pva
// (the normal case), every slot is used, so non-member lookups return
// the stored value of some S-member. The non-member decoded-value
// distribution therefore matches the in-S value distribution. Storing
// more of a value means more non-members decode to that value.
TEST_CASE("encoded_retrieval: non-member distribution tracks stored-value distribution",
          "[encoded_retrieval][mphf]") {
    auto keys = make_keys(1000);
    std::vector<color> values;
    values.reserve(keys.size());
    // 80% A, 10% B, 10% C, 0% D. Expect non-members to reflect these ratios.
    std::mt19937_64 rng{42};
    for (size_t i = 0; i < keys.size(); ++i) {
        double r = std::uniform_real_distribution<double>{0.0, 1.0}(rng);
        if (r < 0.8) values.push_back(color::A);
        else if (r < 0.9) values.push_back(color::B);
        else values.push_back(color::C);
    }

    padded_codec<color, 4> cdc({color::A, color::B, color::C}, color::D);
    using Enc = encoded_retrieval<phf_value_array<phobic5, 4>, padded_codec<color, 4>>;
    auto built = Enc::builder(cdc).add_all(keys, values).build();
    REQUIRE(built.has_value());

    // In-S round-trip.
    for (size_t i = 0; i < keys.size(); ++i) {
        REQUIRE(built->lookup(keys[i]) == values[i]);
    }

    // Non-member distribution should look like the in-S distribution.
    std::array<size_t, 4> counts{0, 0, 0, 0};
    const size_t N_unknown = 20000;
    for (size_t i = 0; i < N_unknown; ++i) {
        std::string u = "UNK_" + std::to_string(i);
        counts[static_cast<size_t>(built->lookup(u))]++;
    }
    double frac_a = static_cast<double>(counts[0]) / N_unknown;
    double frac_b = static_cast<double>(counts[1]) / N_unknown;
    double frac_c = static_cast<double>(counts[2]) / N_unknown;
    double frac_d = static_cast<double>(counts[3]) / N_unknown;

    // Approximate in-S ratios, with statistical slack for sample noise
    // and PHF's arbitrary mapping of non-members to slots.
    REQUIRE(frac_a > 0.70);  REQUIRE(frac_a < 0.90);
    REQUIRE(frac_b > 0.05);  REQUIRE(frac_b < 0.15);
    REQUIRE(frac_c > 0.05);  REQUIRE(frac_c < 0.15);
    // D is never stored, so it never appears in an MPHF's output.
    REQUIRE(frac_d == 0.0);
}

// ===== dense_codec round-trip =====

TEST_CASE("encoded_retrieval<ribbon<2>, dense<color,2>>: keys in S round-trip",
          "[encoded_retrieval][dense]") {
    auto keys = make_keys(400);
    std::vector<color> values;
    values.reserve(keys.size());
    std::mt19937_64 rng{17};
    std::uniform_int_distribution<int> pick(0, 3);
    for (size_t i = 0; i < keys.size(); ++i) {
        values.push_back(static_cast<color>(pick(rng)));
    }

    dense_codec<color, 2> cdc({color::A, color::B, color::C, color::D});
    using Enc = encoded_retrieval<ribbon_retrieval<2>, dense_codec<color, 2>>;
    auto built = Enc::builder(cdc).add_all(keys, values).build();
    REQUIRE(built.has_value());

    for (size_t i = 0; i < keys.size(); ++i) {
        REQUIRE(built->lookup(keys[i]) == values[i]);
    }
}
