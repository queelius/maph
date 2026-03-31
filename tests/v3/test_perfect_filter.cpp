/**
 * @file test_perfect_filter.cpp
 * @brief Tests for perfect_filter composition
 */

#include <catch2/catch_test_macros.hpp>
#include <maph/perfect_filter.hpp>
#include <maph/phobic.hpp>
#include <random>
#include <algorithm>
#include <cmath>

using namespace maph;

namespace {

std::vector<std::string> make_keys(size_t count, uint64_t seed = 42) {
    std::vector<std::string> keys;
    keys.reserve(count);
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<int> char_dist('a', 'z');
    std::uniform_int_distribution<size_t> len_dist(4, 16);
    for (size_t i = 0; i < count; ++i) {
        std::string key;
        size_t len = len_dist(rng);
        key.reserve(len);
        for (size_t j = 0; j < len; ++j) {
            key += static_cast<char>(char_dist(rng));
        }
        keys.push_back(std::move(key));
    }
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    return keys;
}

std::vector<std::string> make_unknowns(size_t count, uint64_t seed = 99999) {
    std::vector<std::string> keys;
    keys.reserve(count);
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<int> char_dist('a', 'z');
    for (size_t i = 0; i < count; ++i) {
        std::string key = "UNKNOWN_";
        for (size_t j = 0; j < 12; ++j) {
            key += static_cast<char>(char_dist(rng));
        }
        keys.push_back(std::move(key));
    }
    return keys;
}

} // namespace

TEST_CASE("perfect_filter: known keys accepted (16-bit)", "[perfect_filter]") {
    auto keys = make_keys(1000);
    auto phf = phobic5::builder{}.add_all(keys).build().value();
    auto pf = perfect_filter<phobic5, 16>::build(std::move(phf), keys);

    for (const auto& key : keys) {
        auto slot = pf.slot_for(key);
        REQUIRE(slot.has_value());
        REQUIRE(slot->value < pf.range_size());
        REQUIRE(pf.contains(key));
    }
}

TEST_CASE("perfect_filter: known keys accepted (8-bit)", "[perfect_filter]") {
    auto keys = make_keys(500);
    auto phf = phobic5::builder{}.add_all(keys).build().value();
    auto pf = perfect_filter<phobic5, 8>::build(std::move(phf), keys);

    for (const auto& key : keys) {
        REQUIRE(pf.contains(key));
        REQUIRE(pf.slot_for(key).has_value());
    }
}

// Note: 10-bit test deferred to after Task 4 (relaxed width constraint)

TEST_CASE("perfect_filter: FP rate within statistical bounds", "[perfect_filter]") {
    auto keys = make_keys(1000);
    auto unknowns = make_unknowns(100000);
    auto phf = phobic5::builder{}.add_all(keys).build().value();
    auto pf = perfect_filter<phobic5, 16>::build(std::move(phf), keys);

    size_t fps = 0;
    for (const auto& uk : unknowns) {
        if (pf.contains(uk)) ++fps;
    }

    double fp_rate = static_cast<double>(fps) / static_cast<double>(unknowns.size());
    double expected = 1.0 / 65536.0;
    double stddev = std::sqrt(expected * (1.0 - expected) / static_cast<double>(unknowns.size()));
    INFO("FP rate: " << fp_rate << ", expected: " << expected << " +/- " << (3 * stddev));
    REQUIRE(fp_rate < expected + 3 * stddev);
}

TEST_CASE("perfect_filter: contains matches slot_for", "[perfect_filter]") {
    auto keys = make_keys(500);
    auto unknowns = make_unknowns(1000);
    auto phf = phobic5::builder{}.add_all(keys).build().value();
    auto pf = perfect_filter<phobic5, 16>::build(std::move(phf), keys);

    for (const auto& key : keys) {
        REQUIRE(pf.contains(key) == pf.slot_for(key).has_value());
    }
    for (const auto& uk : unknowns) {
        REQUIRE(pf.contains(uk) == pf.slot_for(uk).has_value());
    }
}

TEST_CASE("perfect_filter: underlying PHF accessible", "[perfect_filter]") {
    auto keys = make_keys(200);
    auto phf = phobic5::builder{}.add_all(keys).build().value();
    auto pf = perfect_filter<phobic5, 16>::build(std::move(phf), keys);

    const auto& inner = pf.phf();
    for (const auto& key : keys) {
        auto slot = inner.slot_for(key);
        REQUIRE(slot.value < pf.range_size());
    }
}

TEST_CASE("perfect_filter: delegates num_keys and range_size", "[perfect_filter]") {
    auto keys = make_keys(200);
    auto phf = phobic5::builder{}.add_all(keys).build().value();
    size_t expected_n = phf.num_keys();
    size_t expected_m = phf.range_size();
    auto pf = perfect_filter<phobic5, 16>::build(std::move(phf), keys);

    REQUIRE(pf.num_keys() == expected_n);
    REQUIRE(pf.range_size() == expected_m);
}

TEST_CASE("perfect_filter: serialization round-trip", "[perfect_filter]") {
    auto keys = make_keys(500);
    auto phf = phobic5::builder{}.add_all(keys).build().value();
    auto pf = perfect_filter<phobic5, 16>::build(std::move(phf), keys);

    auto bytes = pf.serialize();
    REQUIRE(!bytes.empty());

    auto restored = perfect_filter<phobic5, 16>::deserialize(bytes);
    REQUIRE(restored.has_value());

    for (const auto& key : keys) {
        REQUIRE(restored->contains(key));
        REQUIRE(restored->slot_for(key).has_value());
        REQUIRE(restored->slot_for(key)->value == pf.slot_for(key)->value);
    }
}
