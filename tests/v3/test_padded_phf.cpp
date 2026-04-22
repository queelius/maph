/**
 * @file test_padded_phf.cpp
 * @brief Tests for padded_phf wrapper.
 *
 * Verifies that the wrapper preserves the perfect-hash property on S,
 * produces the requested range expansion, and in composition with
 * encoded_retrieval + padded_codec gives the intended "non-member
 * decodes to default" behavior.
 */

#include <catch2/catch_test_macros.hpp>

#include <maph/algorithms/phobic.hpp>
#include <maph/codecs/padded_codec.hpp>
#include <maph/composition/padded_phf.hpp>
#include <maph/concepts/perfect_hash_function.hpp>
#include <maph/retrieval/encoded_retrieval.hpp>
#include <maph/retrieval/phf_value_array.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <unordered_set>
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

TEST_CASE("padded_phf concept: satisfies perfect_hash_function",
          "[padded_phf][concept]") {
    STATIC_REQUIRE(perfect_hash_function<padded_phf<phobic5>>);
}

TEST_CASE("padded_phf: range is inner.range * padding_factor",
          "[padded_phf]") {
    auto keys = make_keys(1000);
    auto built = padded_phf<phobic5>::builder{}
        .add_all(keys)
        .with_padding(4)
        .build();
    REQUIRE(built.has_value());

    REQUIRE(built->num_keys() == keys.size());
    REQUIRE(built->range_size() == built->inner().range_size() * 4);
    REQUIRE(built->padding_factor() == 4);
}

TEST_CASE("padded_phf: all S-keys map to distinct padded slots",
          "[padded_phf]") {
    auto keys = make_keys(2000);
    auto built = padded_phf<phobic5>::builder{}
        .add_all(keys)
        .with_padding(8)
        .build();
    REQUIRE(built.has_value());

    std::unordered_set<uint64_t> slots;
    for (const auto& k : keys) {
        uint64_t s = built->slot_for(k).value;
        REQUIRE(s < built->range_size());
        REQUIRE(slots.insert(s).second);
    }
}

TEST_CASE("padded_phf: padding_factor=1 acts as pass-through",
          "[padded_phf]") {
    auto keys = make_keys(500);
    auto built = padded_phf<phobic5>::builder{}
        .add_all(keys)
        .with_padding(1)
        .build();
    REQUIRE(built.has_value());

    REQUIRE(built->range_size() == built->inner().range_size());
    for (const auto& k : keys) {
        REQUIRE(built->slot_for(k).value == built->inner().slot_for(k).value);
    }
}

TEST_CASE("padded_phf: serialize/deserialize round-trip",
          "[padded_phf][serialize]") {
    auto keys = make_keys(400);
    auto built = padded_phf<phobic5>::builder{}
        .add_all(keys)
        .with_padding(3)
        .build();
    REQUIRE(built.has_value());

    auto bytes = built->serialize();
    auto restored = padded_phf<phobic5>::deserialize(bytes);
    REQUIRE(restored.has_value());
    REQUIRE(restored->num_keys() == built->num_keys());
    REQUIRE(restored->range_size() == built->range_size());
    REQUIRE(restored->padding_factor() == built->padding_factor());
    for (const auto& k : keys) {
        REQUIRE(restored->slot_for(k).value == built->slot_for(k).value);
    }
}

// The payoff test: encoded_retrieval over pva<padded_phf<phobic5>, M>
// with padded_codec. Non-members whose padded slot is unused decode to
// the codec's default pattern.

TEST_CASE("encoded_retrieval<pva<padded_phf>, padded_codec>: non-members default correctly",
          "[padded_phf][default][integration]") {
    // S = 500 keys each storing A, B, or D (never C).
    auto keys = make_keys(500);
    std::vector<color> values;
    values.reserve(keys.size());
    std::mt19937_64 rng{7};
    std::uniform_int_distribution<int> pick(0, 2);
    for (size_t i = 0; i < keys.size(); ++i) {
        int idx = pick(rng);
        if (idx == 0) values.push_back(color::A);
        else if (idx == 1) values.push_back(color::B);
        else values.push_back(color::D);
    }

    // Codec: A=0, B=1, D=2 indexed; C is the default.
    padded_codec<color, 4> cdc({color::A, color::B, color::D}, color::C);

    using PaddedPHF = padded_phf<phobic5>;
    using PVA = phf_value_array<PaddedPHF, 4>;
    using Enc = encoded_retrieval<PVA, padded_codec<color, 4>>;

    // 4x padding: 75% of the padded range is unused and carries the
    // fill pattern (which decodes to the codec's default, C).
    auto built = Enc::builder(cdc)
        .add_all(std::span<const std::string>{keys}, std::span<const color>{values})
        .with_padding(4)
        .build();
    REQUIRE(built.has_value());

    // All in-S keys round-trip.
    for (size_t i = 0; i < keys.size(); ++i) {
        REQUIRE(built->lookup(keys[i]) == values[i]);
    }

    // Non-members: most decode to C because they land in unused
    // padded slots, which were filled with the pattern decoding to C.
    std::array<size_t, 4> counts{0, 0, 0, 0};
    const size_t N_unknown = 20000;
    for (size_t i = 0; i < N_unknown; ++i) {
        std::string u = "UNK_" + std::to_string(i);
        counts[static_cast<size_t>(built->lookup(u))]++;
    }
    double frac_c = static_cast<double>(counts[2]) / N_unknown;
    // 4x padding: ~75% of slots are default, with sample and PHF
    // distribution slack give a band of [65%, 85%].
    REQUIRE(frac_c > 0.65);
    REQUIRE(frac_c < 0.85);
}

TEST_CASE("encoded_retrieval with padded_phf, large padding: default dominates",
          "[padded_phf][default][integration]") {
    // 16x padding: 15/16 = 93.75% of slots are default.
    auto keys = make_keys(200);
    std::vector<color> values(keys.size(), color::A);

    padded_codec<color, 4> cdc({color::A, color::B, color::D}, color::C);

    using PaddedPHF = padded_phf<phobic5>;
    using PVA = phf_value_array<PaddedPHF, 4>;
    using Enc = encoded_retrieval<PVA, padded_codec<color, 4>>;

    auto built = Enc::builder(cdc)
        .add_all(std::span<const std::string>{keys}, std::span<const color>{values})
        .with_padding(16)
        .build();
    REQUIRE(built.has_value());

    for (size_t i = 0; i < keys.size(); ++i) {
        REQUIRE(built->lookup(keys[i]) == values[i]);
    }

    size_t count_c = 0;
    const size_t N_unknown = 20000;
    for (size_t i = 0; i < N_unknown; ++i) {
        std::string u = "UNK_" + std::to_string(i);
        if (built->lookup(u) == color::C) ++count_c;
    }
    double frac_c = static_cast<double>(count_c) / N_unknown;
    REQUIRE(frac_c > 0.88);
}
