/**
 * @file test_membership.cpp
 * @brief Tests for membership verification strategies
 */

#include <catch2/catch_test_macros.hpp>
#include <maph/membership.hpp>
#include <maph/hashers_perfect.hpp>
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

struct recsplit_fixture {
    recsplit8 hasher;
    std::function<std::optional<size_t>(std::string_view)> slot_fn;

    static recsplit_fixture create(const std::vector<std::string>& keys) {
        auto h = recsplit8::builder{}.add_all(keys).build().value();
        auto fn = [h = std::make_shared<recsplit8>(std::move(h))](std::string_view k) -> std::optional<size_t> {
            auto s = h->slot_for(k);
            return s ? std::optional<size_t>{s->value} : std::nullopt;
        };
        auto h2 = recsplit8::builder{}.add_all(keys).build().value();
        return {std::move(h2), std::move(fn)};
    }
};

} // namespace

// ===== PACKED FINGERPRINT ARRAY TESTS =====

TEST_CASE("packed_fingerprint_array: all widths verify known keys", "[membership][packed]") {
    auto keys = make_keys(500);
    auto fix = recsplit_fixture::create(keys);

    SECTION("8-bit") {
        packed_fingerprint_array<8> pfa;
        pfa.build(keys, fix.slot_fn, keys.size());
        for (const auto& key : keys) {
            auto slot = fix.slot_fn(key);
            REQUIRE(slot.has_value());
            REQUIRE(pfa.verify(key, *slot));
        }
        REQUIRE(pfa.bits_per_key(keys.size()) == 8.0);
    }

    SECTION("16-bit") {
        packed_fingerprint_array<16> pfa;
        pfa.build(keys, fix.slot_fn, keys.size());
        for (const auto& key : keys) {
            auto slot = fix.slot_fn(key);
            REQUIRE(slot.has_value());
            REQUIRE(pfa.verify(key, *slot));
        }
        REQUIRE(pfa.bits_per_key(keys.size()) == 16.0);
    }

    SECTION("32-bit") {
        packed_fingerprint_array<32> pfa;
        pfa.build(keys, fix.slot_fn, keys.size());
        for (const auto& key : keys) {
            auto slot = fix.slot_fn(key);
            REQUIRE(slot.has_value());
            REQUIRE(pfa.verify(key, *slot));
        }
        REQUIRE(pfa.bits_per_key(keys.size()) == 32.0);
    }
}

TEST_CASE("packed_fingerprint_array: FP rate within statistical bounds", "[membership][packed]") {
    auto keys = make_keys(1000);
    auto unknowns = make_unknowns(100000);
    auto fix = recsplit_fixture::create(keys);

    packed_fingerprint_array<16> pfa;
    pfa.build(keys, fix.slot_fn, keys.size());

    size_t false_positives = 0;
    for (const auto& uk : unknowns) {
        size_t arbitrary_slot = fix.hasher.hash(uk).value % keys.size();
        if (pfa.verify(uk, arbitrary_slot)) ++false_positives;
    }

    double fp_rate = static_cast<double>(false_positives) / unknowns.size();
    double expected = 1.0 / 65536.0;
    double n = static_cast<double>(unknowns.size());
    double stddev = std::sqrt(expected * (1.0 - expected) / n);

    INFO("FP rate: " << fp_rate << ", expected: " << expected << " +/- " << (3 * stddev));
    REQUIRE(fp_rate < expected + 3 * stddev);
}

TEST_CASE("packed_fingerprint_array: serialization round-trip", "[membership][packed]") {
    auto keys = make_keys(200);
    auto fix = recsplit_fixture::create(keys);

    packed_fingerprint_array<16> original;
    original.build(keys, fix.slot_fn, keys.size());

    auto bytes = original.serialize();
    auto restored = packed_fingerprint_array<16>::deserialize(bytes);
    REQUIRE(restored.has_value());

    for (const auto& key : keys) {
        auto slot = fix.slot_fn(key);
        REQUIRE(slot.has_value());
        REQUIRE(restored->verify(key, *slot));
    }
}

TEST_CASE("packed_fingerprint_array: edge cases", "[membership][packed]") {
    SECTION("Single key") {
        std::vector<std::string> keys = {"only_key"};
        auto fix = recsplit_fixture::create(keys);
        packed_fingerprint_array<8> pfa;
        pfa.build(keys, fix.slot_fn, keys.size());
        REQUIRE(pfa.verify("only_key", fix.slot_fn("only_key").value()));
    }

    SECTION("Duplicate keys deduplicated by caller") {
        std::vector<std::string> keys = {"aaa", "bbb", "aaa", "ccc"};
        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
        auto fix = recsplit_fixture::create(keys);
        packed_fingerprint_array<8> pfa;
        pfa.build(keys, fix.slot_fn, keys.size());
        for (const auto& key : keys) {
            REQUIRE(pfa.verify(key, fix.slot_fn(key).value()));
        }
    }

    SECTION("Very long keys") {
        std::vector<std::string> keys;
        for (int i = 0; i < 10; ++i) {
            keys.push_back(std::string(1000, 'a' + i));
        }
        auto fix = recsplit_fixture::create(keys);
        packed_fingerprint_array<16> pfa;
        pfa.build(keys, fix.slot_fn, keys.size());
        for (const auto& key : keys) {
            REQUIRE(pfa.verify(key, fix.slot_fn(key).value()));
        }
    }
}
