/**
 * @file test_binary_fuse.cpp
 * @brief Tests for binary_fuse_filter.
 */

#include <catch2/catch_test_macros.hpp>

#include <maph/filters/binary_fuse_filter.hpp>

#include <algorithm>
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
    std::uniform_int_distribution<size_t> len_dist(6, 24);
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

} // namespace

TEST_CASE("binary_fuse_filter<8>: accepts all keys in S", "[binary_fuse]") {
    auto keys = make_keys(5000);
    binary_fuse_filter<8> f;
    REQUIRE(f.build(keys));
    for (const auto& k : keys) {
        REQUIRE(f.verify(k));
    }
}

TEST_CASE("binary_fuse_filter<16>: accepts all keys in S", "[binary_fuse]") {
    auto keys = make_keys(5000);
    binary_fuse_filter<16> f;
    REQUIRE(f.build(keys));
    for (const auto& k : keys) {
        REQUIRE(f.verify(k));
    }
}

TEST_CASE("binary_fuse_filter<32>: accepts all keys in S", "[binary_fuse]") {
    auto keys = make_keys(2000);
    binary_fuse_filter<32> f;
    REQUIRE(f.build(keys));
    for (const auto& k : keys) {
        REQUIRE(f.verify(k));
    }
}

TEST_CASE("binary_fuse_filter<8>: FPR near 2^-8 ~ 0.39%", "[binary_fuse][fpr]") {
    auto keys = make_keys(10000);
    binary_fuse_filter<8> f;
    REQUIRE(f.build(keys));

    auto unknowns = make_unknowns(100000);
    size_t false_positives = 0;
    for (const auto& u : unknowns) {
        if (f.verify(u)) ++false_positives;
    }
    double fpr = static_cast<double>(false_positives) / unknowns.size();
    // Expected ~1/256 = 0.39%. Give generous bounds to account for sampling.
    REQUIRE(fpr > 0.002);
    REQUIRE(fpr < 0.008);
}

TEST_CASE("binary_fuse_filter<16>: FPR near 2^-16", "[binary_fuse][fpr]") {
    auto keys = make_keys(10000);
    binary_fuse_filter<16> f;
    REQUIRE(f.build(keys));

    auto unknowns = make_unknowns(200000);
    size_t false_positives = 0;
    for (const auto& u : unknowns) {
        if (f.verify(u)) ++false_positives;
    }
    double fpr = static_cast<double>(false_positives) / unknowns.size();
    // Expected ~1/65536 = 0.0015%. With 200K samples, expect 3 FPs on average.
    REQUIRE(fpr <= 0.0005);
}

TEST_CASE("binary_fuse_filter<8>: bits_per_key around 1.125 * 8 = 9 b/k",
          "[binary_fuse][space]") {
    auto keys = make_keys(20000);
    binary_fuse_filter<8> f;
    REQUIRE(f.build(keys));
    double bpk = f.bits_per_key(keys.size());
    // At this scale the size_factor is close to 1.125 and segment overhead
    // adds a tail, giving something in [9, 10] bits/key in practice.
    REQUIRE(bpk >= 9.0);
    REQUIRE(bpk <= 11.0);
}

TEST_CASE("binary_fuse_filter<8>: serialize/deserialize round-trip",
          "[binary_fuse][serialize]") {
    auto keys = make_keys(1000);
    binary_fuse_filter<8> f;
    REQUIRE(f.build(keys));

    auto bytes = f.serialize();
    REQUIRE(!bytes.empty());

    auto restored = binary_fuse_filter<8>::deserialize(bytes);
    REQUIRE(restored.has_value());

    for (const auto& k : keys) {
        REQUIRE(restored->verify(k) == f.verify(k));
    }
}
