/**
 * @file test_retrieval.cpp
 * @brief Tests for the retrieval concept and phf_value_array adapter.
 *
 * The retrieval concept is the primitive behind static-function data
 * structures: for keys in S, lookup returns the stored value; for keys
 * not in S, lookup returns garbage (garbage in, garbage out). Verifies
 * the adapter reports correct values on S, makes no out-of-bounds
 * accesses on non-S, and round-trips through serialization.
 */

#include <catch2/catch_test_macros.hpp>

#include <maph/algorithms/bbhash.hpp>
#include <maph/algorithms/phobic.hpp>
#include <maph/concepts/retrieval.hpp>
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

template <unsigned M>
uint64_t deterministic_value_for(std::string_view key, uint64_t seed = 0x9e3779b97f4a7c15ULL) {
    // splitmix-ish hash on bytes, truncated to M bits.
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

// ===== Concept satisfaction =====

TEST_CASE("retrieval concept: phf_value_array satisfies retrieval", "[retrieval][concept]") {
    STATIC_REQUIRE(retrieval<phf_value_array<phobic5, 1>>);
    STATIC_REQUIRE(retrieval<phf_value_array<phobic5, 8>>);
    STATIC_REQUIRE(retrieval<phf_value_array<phobic5, 16>>);
    STATIC_REQUIRE(retrieval<phf_value_array<phobic5, 32>>);
    STATIC_REQUIRE(retrieval<phf_value_array<phobic5, 64>>);
    STATIC_REQUIRE(retrieval<phf_value_array<bbhash5, 8>>);
}

// ===== Retrieval correctness =====

TEST_CASE("phf_value_array<phobic5, 8>: returns stored values for keys in S", "[retrieval][phobic]") {
    auto keys = make_keys(2000);
    std::vector<uint8_t> values;
    values.reserve(keys.size());
    for (const auto& k : keys) values.push_back(static_cast<uint8_t>(deterministic_value_for<8>(k)));

    auto built = phf_value_array<phobic5, 8>::builder{}
        .add_all(keys, values)
        .build();
    REQUIRE(built.has_value());

    for (size_t i = 0; i < keys.size(); ++i) {
        REQUIRE(built->lookup(keys[i]) == values[i]);
    }
}

TEST_CASE("phf_value_array<phobic5, 16>: returns stored values for keys in S", "[retrieval][phobic]") {
    auto keys = make_keys(1500);
    std::vector<uint16_t> values;
    values.reserve(keys.size());
    for (const auto& k : keys) values.push_back(static_cast<uint16_t>(deterministic_value_for<16>(k)));

    auto built = phf_value_array<phobic5, 16>::builder{}
        .add_all(keys, values)
        .build();
    REQUIRE(built.has_value());

    for (size_t i = 0; i < keys.size(); ++i) {
        REQUIRE(built->lookup(keys[i]) == values[i]);
    }
}

TEST_CASE("phf_value_array<phobic5, 1>: single-bit retrieval", "[retrieval][phobic]") {
    auto keys = make_keys(1000);
    std::vector<uint8_t> values;
    values.reserve(keys.size());
    for (const auto& k : keys) values.push_back(static_cast<uint8_t>(deterministic_value_for<1>(k)));

    auto built = phf_value_array<phobic5, 1>::builder{}
        .add_all(keys, values)
        .build();
    REQUIRE(built.has_value());

    for (size_t i = 0; i < keys.size(); ++i) {
        REQUIRE(built->lookup(keys[i]) == values[i]);
    }
}

TEST_CASE("phf_value_array<phobic5, 32>: 32-bit retrieval", "[retrieval][phobic]") {
    auto keys = make_keys(500);
    std::vector<uint32_t> values;
    values.reserve(keys.size());
    for (const auto& k : keys) values.push_back(static_cast<uint32_t>(deterministic_value_for<32>(k)));

    auto built = phf_value_array<phobic5, 32>::builder{}
        .add_all(keys, values)
        .build();
    REQUIRE(built.has_value());

    for (size_t i = 0; i < keys.size(); ++i) {
        REQUIRE(built->lookup(keys[i]) == values[i]);
    }
}

TEST_CASE("phf_value_array<bbhash5, 16>: works with a different PHF", "[retrieval][bbhash]") {
    auto keys = make_keys(1000);
    std::vector<uint16_t> values;
    values.reserve(keys.size());
    for (const auto& k : keys) values.push_back(static_cast<uint16_t>(deterministic_value_for<16>(k)));

    auto built = phf_value_array<bbhash5, 16>::builder{}
        .add_all(keys, values)
        .build();
    REQUIRE(built.has_value());

    for (size_t i = 0; i < keys.size(); ++i) {
        REQUIRE(built->lookup(keys[i]) == values[i]);
    }
}

// ===== Builder variants =====

TEST_CASE("phf_value_array: add_all_with(ValueFn) fills values from a function", "[retrieval]") {
    auto keys = make_keys(500);
    auto built = phf_value_array<phobic5, 16>::builder{}
        .add_all_with(std::span<const std::string>{keys},
                      [](std::string_view k){ return deterministic_value_for<16>(k); })
        .build();
    REQUIRE(built.has_value());

    for (const auto& k : keys) {
        REQUIRE(built->lookup(k) == static_cast<uint16_t>(deterministic_value_for<16>(k)));
    }
}

TEST_CASE("phf_value_array: per-key add(key, value) incremental", "[retrieval]") {
    auto keys = make_keys(300);
    typename phf_value_array<phobic5, 8>::builder b;
    std::vector<uint8_t> expected;
    expected.reserve(keys.size());
    for (const auto& k : keys) {
        uint8_t v = static_cast<uint8_t>(deterministic_value_for<8>(k));
        expected.push_back(v);
        b.add(k, v);
    }
    auto built = b.build();
    REQUIRE(built.has_value());
    for (size_t i = 0; i < keys.size(); ++i) {
        REQUIRE(built->lookup(keys[i]) == expected[i]);
    }
}

// ===== GIGO semantics =====

TEST_CASE("phf_value_array: non-member lookups are well-defined (GIGO)", "[retrieval][gigo]") {
    auto keys = make_keys(1000);
    std::vector<uint16_t> values;
    values.reserve(keys.size());
    for (const auto& k : keys) values.push_back(static_cast<uint16_t>(deterministic_value_for<16>(k)));

    auto built = phf_value_array<phobic5, 16>::builder{}
        .add_all(keys, values)
        .build();
    REQUIRE(built.has_value());

    // Generate keys guaranteed to be outside S.
    std::vector<std::string> unknowns;
    unknowns.reserve(500);
    for (size_t i = 0; i < 500; ++i) {
        unknowns.push_back("NONMEMBER_" + std::to_string(i));
    }

    // The critical property: lookups on non-members return *some* value
    // without throwing, branching, or accessing out-of-bounds memory.
    // We don't care what the value is. We only require that the call
    // returns (i.e., exercises the GIGO path without UB).
    for (const auto& u : unknowns) {
        volatile uint16_t v = built->lookup(u);
        (void)v;
    }

    // Additionally: the full output range of M=16 bits is reachable in
    // principle, so we merely check that the call completes. This is a
    // regression test against accidental bounds checks or sentinels.
    SUCCEED();
}

// ===== Space =====

TEST_CASE("phf_value_array: bits_per_key is PHF cost + M", "[retrieval][space]") {
    auto keys = make_keys(2000);
    std::vector<uint16_t> values(keys.size(), 0);
    for (size_t i = 0; i < keys.size(); ++i) values[i] = static_cast<uint16_t>(i & 0xFFFF);

    auto built = phf_value_array<phobic5, 16>::builder{}
        .add_all(keys, values)
        .build();
    REQUIRE(built.has_value());

    double bpk = built->bits_per_key();
    // phobic5 is around 2.7 bits/key; 16 bits for the value payload; plus
    // some slack for range_size > num_keys in non-minimal PHOBIC builds.
    REQUIRE(bpk > 16.0);
    REQUIRE(bpk < 25.0);
    REQUIRE(built->value_bits() == 16);
    REQUIRE(built->num_keys() == keys.size());
}

// ===== Serialization round-trip =====

// ===== ribbon_retrieval =====

TEST_CASE("ribbon_retrieval concept: satisfies retrieval", "[retrieval][concept][ribbon]") {
    STATIC_REQUIRE(retrieval<ribbon_retrieval<1>>);
    STATIC_REQUIRE(retrieval<ribbon_retrieval<8>>);
    STATIC_REQUIRE(retrieval<ribbon_retrieval<16>>);
    STATIC_REQUIRE(retrieval<ribbon_retrieval<32>>);
    STATIC_REQUIRE(retrieval<ribbon_retrieval<64>>);
}

TEST_CASE("ribbon_retrieval<8>: returns stored values for keys in S", "[retrieval][ribbon]") {
    auto keys = make_keys(2000);
    std::vector<uint8_t> values;
    values.reserve(keys.size());
    for (const auto& k : keys) values.push_back(static_cast<uint8_t>(deterministic_value_for<8>(k)));

    auto built = ribbon_retrieval<8>::builder{}
        .add_all(keys, values)
        .build();
    REQUIRE(built.has_value());

    for (size_t i = 0; i < keys.size(); ++i) {
        REQUIRE(built->lookup(keys[i]) == values[i]);
    }
}

TEST_CASE("ribbon_retrieval<16>: returns stored values for keys in S", "[retrieval][ribbon]") {
    auto keys = make_keys(1500);
    std::vector<uint16_t> values;
    values.reserve(keys.size());
    for (const auto& k : keys) values.push_back(static_cast<uint16_t>(deterministic_value_for<16>(k)));

    auto built = ribbon_retrieval<16>::builder{}
        .add_all(keys, values)
        .build();
    REQUIRE(built.has_value());

    for (size_t i = 0; i < keys.size(); ++i) {
        REQUIRE(built->lookup(keys[i]) == values[i]);
    }
}

TEST_CASE("ribbon_retrieval<1>: single-bit values round-trip", "[retrieval][ribbon]") {
    auto keys = make_keys(1000);
    std::vector<uint8_t> values;
    values.reserve(keys.size());
    for (const auto& k : keys) values.push_back(static_cast<uint8_t>(deterministic_value_for<1>(k)));

    auto built = ribbon_retrieval<1>::builder{}
        .add_all(keys, values)
        .build();
    REQUIRE(built.has_value());

    for (size_t i = 0; i < keys.size(); ++i) {
        REQUIRE((built->lookup(keys[i]) & 1u) == (values[i] & 1u));
    }
}

TEST_CASE("ribbon_retrieval<32>: 32-bit values round-trip", "[retrieval][ribbon]") {
    auto keys = make_keys(500);
    std::vector<uint32_t> values;
    values.reserve(keys.size());
    for (const auto& k : keys) values.push_back(static_cast<uint32_t>(deterministic_value_for<32>(k)));

    auto built = ribbon_retrieval<32>::builder{}
        .add_all(keys, values)
        .build();
    REQUIRE(built.has_value());

    for (size_t i = 0; i < keys.size(); ++i) {
        REQUIRE(built->lookup(keys[i]) == values[i]);
    }
}

TEST_CASE("ribbon_retrieval: space is ~1.04 M bits/key", "[retrieval][ribbon][space]") {
    auto keys = make_keys(5000);
    std::vector<uint16_t> values(keys.size(), 0);
    for (size_t i = 0; i < keys.size(); ++i) values[i] = static_cast<uint16_t>(i & 0xFFFF);

    auto built = ribbon_retrieval<16>::builder{}
        .add_all(keys, values)
        .build();
    REQUIRE(built.has_value());

    // Theoretical floor is 16 bits/key (one value_bits per key).
    // Ribbon overhead is small: at epsilon=0.08 we expect ~17.3 bits/key.
    double bpk = built->bits_per_key();
    REQUIRE(bpk >= 16.0);
    REQUIRE(bpk <= 20.0);
    REQUIRE(built->value_bits() == 16);
    REQUIRE(built->num_keys() == keys.size());
}

TEST_CASE("ribbon_retrieval: non-member lookups are well-defined (GIGO)", "[retrieval][ribbon][gigo]") {
    auto keys = make_keys(1000);
    std::vector<uint16_t> values;
    values.reserve(keys.size());
    for (const auto& k : keys) values.push_back(static_cast<uint16_t>(deterministic_value_for<16>(k)));

    auto built = ribbon_retrieval<16>::builder{}
        .add_all(keys, values)
        .build();
    REQUIRE(built.has_value());

    // Non-members: query must return *some* value without UB.
    for (size_t i = 0; i < 500; ++i) {
        std::string u = "NONMEMBER_" + std::to_string(i);
        volatile uint16_t v = built->lookup(u);
        (void)v;
    }
    SUCCEED();
}

TEST_CASE("ribbon_retrieval: serialize/deserialize round-trip", "[retrieval][ribbon][serialize]") {
    auto keys = make_keys(800);
    std::vector<uint16_t> values;
    values.reserve(keys.size());
    for (const auto& k : keys) values.push_back(static_cast<uint16_t>(deterministic_value_for<16>(k)));

    auto built = ribbon_retrieval<16>::builder{}
        .add_all(keys, values)
        .build();
    REQUIRE(built.has_value());

    auto bytes = built->serialize();
    REQUIRE(!bytes.empty());

    auto restored = ribbon_retrieval<16>::deserialize(bytes);
    REQUIRE(restored.has_value());

    for (size_t i = 0; i < keys.size(); ++i) {
        REQUIRE(restored->lookup(keys[i]) == built->lookup(keys[i]));
    }
    REQUIRE(restored->num_keys() == built->num_keys());
    REQUIRE(restored->value_bits() == built->value_bits());
}

TEST_CASE("ribbon_retrieval vs phf_value_array: same semantic output on S", "[retrieval][ribbon][compare]") {
    auto keys = make_keys(500);
    std::vector<uint16_t> values;
    values.reserve(keys.size());
    for (const auto& k : keys) values.push_back(static_cast<uint16_t>(deterministic_value_for<16>(k)));

    auto a = ribbon_retrieval<16>::builder{}.add_all(keys, values).build();
    auto b = phf_value_array<phobic5, 16>::builder{}.add_all(keys, values).build();
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());

    // Both must reproduce v(k) exactly on S; their internal state differs,
    // but the observable retrieval semantics must match.
    for (size_t i = 0; i < keys.size(); ++i) {
        REQUIRE(a->lookup(keys[i]) == values[i]);
        REQUIRE(b->lookup(keys[i]) == values[i]);
    }

    // Ribbon should be more space-efficient than PHF+array at M=16.
    REQUIRE(a->bits_per_key() < b->bits_per_key());
}

TEST_CASE("phf_value_array: serialize/deserialize round-trip", "[retrieval][serialize]") {
    auto keys = make_keys(300);
    std::vector<uint16_t> values;
    values.reserve(keys.size());
    for (const auto& k : keys) values.push_back(static_cast<uint16_t>(deterministic_value_for<16>(k)));

    auto built = phf_value_array<phobic5, 16>::builder{}
        .add_all(keys, values)
        .build();
    REQUIRE(built.has_value());

    auto bytes = built->serialize();
    REQUIRE(!bytes.empty());

    auto restored = phf_value_array<phobic5, 16>::deserialize(bytes);
    REQUIRE(restored.has_value());

    for (size_t i = 0; i < keys.size(); ++i) {
        REQUIRE(restored->lookup(keys[i]) == built->lookup(keys[i]));
    }

    REQUIRE(restored->num_keys() == built->num_keys());
    REQUIRE(restored->value_bits() == built->value_bits());
}
