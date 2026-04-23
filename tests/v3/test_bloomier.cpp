/**
 * @file test_bloomier.cpp
 * @brief Tests for bloomier<Retrieval, Oracle> composition.
 */

#include <catch2/catch_test_macros.hpp>

#include <maph/algorithms/phobic.hpp>
#include <maph/composition/bloomier.hpp>
#include <maph/filters/binary_fuse_filter.hpp>
#include <maph/filters/ribbon_filter.hpp>
#include <maph/filters/xor_filter.hpp>
#include <maph/retrieval/phf_value_array.hpp>
#include <maph/retrieval/ribbon_retrieval.hpp>

#include <algorithm>
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
    std::uniform_int_distribution<size_t> len_dist(6, 16);
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

std::vector<std::string> make_unknowns(size_t count, uint64_t seed = 99999) {
    std::vector<std::string> keys;
    keys.reserve(count);
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<int> char_dist('A', 'Z');
    for (size_t i = 0; i < count; ++i) {
        std::string k = "UNK_";
        for (int j = 0; j < 12; ++j) k.push_back(static_cast<char>(char_dist(rng)));
        keys.push_back(std::move(k));
    }
    return keys;
}

template <unsigned M>
uint64_t det_value(std::string_view key, uint64_t seed = 0x9e3779b97f4a7c15ULL) {
    uint64_t h = seed;
    for (char c : key) {
        h ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        h *= 0xbf58476d1ce4e5b9ULL;
        h ^= h >> 31;
    }
    uint64_t mask = (M == 64) ? ~uint64_t{0} : ((uint64_t{1} << M) - 1);
    return h & mask;
}

} // namespace

TEST_CASE("bloomier<ribbon<16>, xor<8>>: S-keys return stored values",
          "[bloomier]") {
    auto keys = make_keys(1000);
    std::vector<uint16_t> values;
    values.reserve(keys.size());
    for (const auto& k : keys) values.push_back(static_cast<uint16_t>(det_value<16>(k)));

    using B = bloomier<ribbon_retrieval<16>, xor_filter<8>>;
    auto built = B::builder{}.add_all(keys, values).build();
    REQUIRE(built.has_value());

    for (size_t i = 0; i < keys.size(); ++i) {
        auto r = built->lookup(keys[i]);
        REQUIRE(r.has_value());
        REQUIRE(*r == values[i]);
    }
}

TEST_CASE("bloomier<pva<phobic5,8>, ribbon<8>>: S-keys return stored values",
          "[bloomier]") {
    auto keys = make_keys(500);
    std::vector<uint8_t> values;
    values.reserve(keys.size());
    for (const auto& k : keys) values.push_back(static_cast<uint8_t>(det_value<8>(k)));

    using B = bloomier<phf_value_array<phobic5, 8>, ribbon_filter<8>>;
    auto built = B::builder{}.add_all(keys, values).build();
    REQUIRE(built.has_value());

    for (size_t i = 0; i < keys.size(); ++i) {
        auto r = built->lookup(keys[i]);
        REQUIRE(r.has_value());
        REQUIRE(*r == values[i]);
    }
}

TEST_CASE("bloomier<ribbon<16>, binary_fuse<8>>: S-keys return stored values",
          "[bloomier]") {
    auto keys = make_keys(2000);
    std::vector<uint16_t> values;
    values.reserve(keys.size());
    for (const auto& k : keys) values.push_back(static_cast<uint16_t>(det_value<16>(k)));

    using B = bloomier<ribbon_retrieval<16>, binary_fuse_filter<8>>;
    auto built = B::builder{}.add_all(keys, values).build();
    REQUIRE(built.has_value());

    for (size_t i = 0; i < keys.size(); ++i) {
        auto r = built->lookup(keys[i]);
        REQUIRE(r.has_value());
        REQUIRE(*r == values[i]);
    }
}

TEST_CASE("bloomier: non-members mostly rejected (FPR bound)",
          "[bloomier][fpr]") {
    // xor<8> has FPR ~ 1/256 ~ 0.4%. bloomier's rejection rate on
    // non-members should match.
    auto keys = make_keys(5000);
    std::vector<uint16_t> values(keys.size(), 0);
    for (size_t i = 0; i < keys.size(); ++i)
        values[i] = static_cast<uint16_t>(det_value<16>(keys[i]));

    using B = bloomier<ribbon_retrieval<16>, xor_filter<8>>;
    auto built = B::builder{}.add_all(keys, values).build();
    REQUIRE(built.has_value());

    auto unknowns = make_unknowns(50000);
    size_t false_positives = 0;
    for (const auto& u : unknowns) {
        if (built->lookup(u).has_value()) ++false_positives;
    }
    double fpr = static_cast<double>(false_positives) / unknowns.size();
    REQUIRE(fpr < 0.01);  // well under 1%
    REQUIRE(fpr > 0.0005);  // at least something (xor's 1/256 expected)
}

TEST_CASE("bloomier: contains() agrees with lookup().has_value()",
          "[bloomier]") {
    auto keys = make_keys(500);
    std::vector<uint8_t> values(keys.size(), 0);
    for (size_t i = 0; i < keys.size(); ++i)
        values[i] = static_cast<uint8_t>(det_value<8>(keys[i]));

    using B = bloomier<ribbon_retrieval<8>, xor_filter<8>>;
    auto built = B::builder{}.add_all(keys, values).build();
    REQUIRE(built.has_value());

    auto unknowns = make_unknowns(2000);
    std::vector<std::string> mixed;
    mixed.insert(mixed.end(), keys.begin(), keys.begin() + 100);
    mixed.insert(mixed.end(), unknowns.begin(), unknowns.begin() + 500);

    for (const auto& k : mixed) {
        REQUIRE(built->contains(k) == built->lookup(k).has_value());
    }
}

TEST_CASE("bloomier: bits_per_key is retrieval + oracle",
          "[bloomier][space]") {
    auto keys = make_keys(2000);
    std::vector<uint16_t> values;
    values.reserve(keys.size());
    for (const auto& k : keys) values.push_back(static_cast<uint16_t>(det_value<16>(k)));

    using B = bloomier<ribbon_retrieval<16>, xor_filter<8>>;
    auto built = B::builder{}.add_all(keys, values).build();
    REQUIRE(built.has_value());

    double bpk = built->bits_per_key();
    // ribbon<16> ~= 17.3 b/k; xor<8> ~= 9.84 b/k. Total ~ 27.
    REQUIRE(bpk > 20.0);
    REQUIRE(bpk < 40.0);
}
