/**
 * @file test_shock_hash.cpp
 * @brief Tests for shock_hash.
 */

#include <catch2/catch_test_macros.hpp>

#include <maph/algorithms/shock_hash.hpp>
#include <maph/concepts/perfect_hash_function.hpp>

#include <algorithm>
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

} // namespace

TEST_CASE("shock_hash<64> concept: satisfies perfect_hash_function",
          "[shock_hash][concept]") {
    STATIC_REQUIRE(perfect_hash_function<shock_hash<64>>);
}

TEST_CASE("shock_hash<64>: 500 keys map to distinct slots", "[shock_hash]") {
    auto keys = make_keys(500);
    auto built = shock_hash<64>::builder{}
        .add_all(keys)
        .build();
    REQUIRE(built.has_value());

    std::unordered_set<uint64_t> slots;
    for (const auto& k : keys) {
        uint64_t s = built->slot_for(k).value;
        REQUIRE(s < built->range_size());
        REQUIRE(slots.insert(s).second);
    }
}

TEST_CASE("shock_hash<64>: 5000 keys map to distinct slots", "[shock_hash]") {
    auto keys = make_keys(5000);
    auto built = shock_hash<64>::builder{}
        .add_all(keys)
        .build();
    REQUIRE(built.has_value());

    std::unordered_set<uint64_t> slots;
    for (const auto& k : keys) {
        uint64_t s = built->slot_for(k).value;
        REQUIRE(s < built->range_size());
        REQUIRE(slots.insert(s).second);
    }
}

TEST_CASE("shock_hash<128>: 5000 keys", "[shock_hash]") {
    auto keys = make_keys(5000);
    auto built = shock_hash<128>::builder{}
        .add_all(keys)
        .build();
    REQUIRE(built.has_value());

    std::unordered_set<uint64_t> slots;
    for (const auto& k : keys) {
        uint64_t s = built->slot_for(k).value;
        REQUIRE(s < built->range_size());
        REQUIRE(slots.insert(s).second);
    }
}

TEST_CASE("shock_hash<64>: bits_per_key below 2.5", "[shock_hash][space]") {
    auto keys = make_keys(10000);
    auto built = shock_hash<64>::builder{}
        .add_all(keys)
        .build();
    REQUIRE(built.has_value());

    double bpk = built->bits_per_key();
    REQUIRE(bpk < 2.5);  // beats PHOBIC's 2.7
    REQUIRE(bpk > 1.0);  // below theoretical floor would be impossible
}

TEST_CASE("shock_hash<64>: serialize/deserialize round-trip",
          "[shock_hash][serialize]") {
    auto keys = make_keys(500);
    auto built = shock_hash<64>::builder{}
        .add_all(keys)
        .build();
    REQUIRE(built.has_value());

    auto bytes = built->serialize();
    auto restored = shock_hash<64>::deserialize(bytes);
    REQUIRE(restored.has_value());

    for (const auto& k : keys) {
        REQUIRE(restored->slot_for(k).value == built->slot_for(k).value);
    }
    REQUIRE(restored->num_keys() == built->num_keys());
    REQUIRE(restored->range_size() == built->range_size());
}
