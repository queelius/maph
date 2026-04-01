/**
 * @file test_perfect_hash.cpp
 * @brief Tests for perfect hash implementations under the clean PHF concept
 */

#include <catch2/catch_test_macros.hpp>
#include <maph/hashers_perfect.hpp>
#include <maph/phf_concept.hpp>
#include <maph/perfect_filter.hpp>
#include <random>
#include <algorithm>
#include <set>

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

template<typename PHF>
bool verify_bijectivity(const PHF& phf, const std::vector<std::string>& keys) {
    std::set<uint64_t> seen;
    for (const auto& key : keys) {
        auto slot = phf.slot_for(key);
        if (slot.value >= phf.range_size()) return false;
        if (!seen.insert(slot.value).second) return false;
    }
    return seen.size() == keys.size();
}

} // namespace

// ===== CONCEPT SATISFACTION =====

static_assert(perfect_hash_function<recsplit_hasher<8>>);
static_assert(perfect_hash_function<chd_hasher>);
static_assert(perfect_hash_function<bbhash_hasher<3>>);
static_assert(perfect_hash_function<fch_hasher>);
static_assert(perfect_hash_function<pthash_hasher<98>>);

// ===== RECSPLIT TESTS =====

TEST_CASE("RecSplit: bijectivity", "[recsplit]") {
    SECTION("Small key set") {
        std::vector<std::string> keys = {"alpha", "beta", "gamma", "delta", "epsilon"};
        auto phf = recsplit_hasher<8>::builder{}.add_all(keys).build();
        REQUIRE(phf.has_value());
        REQUIRE(verify_bijectivity(*phf, keys));
        REQUIRE(phf->num_keys() == 5);
    }

    SECTION("1000 keys") {
        auto keys = make_keys(1000);
        auto phf = recsplit_hasher<8>::builder{}.add_all(keys).build();
        REQUIRE(phf.has_value());
        REQUIRE(verify_bijectivity(*phf, keys));
    }
}

TEST_CASE("RecSplit: space efficiency", "[recsplit]") {
    auto keys = make_keys(10000);
    auto phf = recsplit_hasher<8>::builder{}.add_all(keys).build();
    REQUIRE(phf.has_value());
    INFO("RecSplit bits/key: " << phf->bits_per_key());
    REQUIRE(phf->bits_per_key() > 0.0);
    REQUIRE(phf->bits_per_key() < 100.0);  // Should be much less without fingerprints
}

TEST_CASE("RecSplit: serialization round-trip", "[recsplit]") {
    auto keys = make_keys(500);
    auto phf = recsplit_hasher<8>::builder{}.add_all(keys).build();
    REQUIRE(phf.has_value());
    auto bytes = phf->serialize();
    auto restored = recsplit_hasher<8>::deserialize(bytes);
    REQUIRE(restored.has_value());
    for (const auto& key : keys) {
        REQUIRE(restored->slot_for(key).value == phf->slot_for(key).value);
    }
}

TEST_CASE("RecSplit: works with perfect_filter", "[recsplit]") {
    auto keys = make_keys(500);
    auto phf = recsplit_hasher<8>::builder{}.add_all(keys).build();
    REQUIRE(phf.has_value());
    auto pf = perfect_filter<recsplit_hasher<8>, 16>::build(std::move(*phf), keys);
    for (const auto& key : keys) {
        REQUIRE(pf.contains(key));
        REQUIRE(pf.slot_for(key).has_value());
    }
}

// ===== CHD TESTS =====

TEST_CASE("CHD: bijectivity", "[chd]") {
    SECTION("Small key set") {
        std::vector<std::string> keys = {"alpha", "beta", "gamma", "delta", "epsilon"};
        auto phf = chd_hasher::builder{}.add_all(keys).build();
        REQUIRE(phf.has_value());
        REQUIRE(verify_bijectivity(*phf, keys));
        REQUIRE(phf->num_keys() == 5);
    }

    SECTION("1000 keys") {
        auto keys = make_keys(1000);
        auto phf = chd_hasher::builder{}.add_all(keys).build();
        REQUIRE(phf.has_value());
        REQUIRE(verify_bijectivity(*phf, keys));
    }
}

TEST_CASE("CHD: space efficiency", "[chd]") {
    auto keys = make_keys(10000);
    auto phf = chd_hasher::builder{}.add_all(keys).build();
    REQUIRE(phf.has_value());
    INFO("CHD bits/key: " << phf->bits_per_key());
    REQUIRE(phf->bits_per_key() > 0.0);
    REQUIRE(phf->bits_per_key() < 200.0);
}

TEST_CASE("CHD: serialization round-trip", "[chd]") {
    auto keys = make_keys(500);
    auto phf = chd_hasher::builder{}.add_all(keys).build();
    REQUIRE(phf.has_value());
    auto bytes = phf->serialize();
    auto restored = chd_hasher::deserialize(bytes);
    REQUIRE(restored.has_value());
    for (const auto& key : keys) {
        REQUIRE(restored->slot_for(key).value == phf->slot_for(key).value);
    }
}

TEST_CASE("CHD: works with perfect_filter", "[chd]") {
    auto keys = make_keys(500);
    auto phf = chd_hasher::builder{}.add_all(keys).build();
    REQUIRE(phf.has_value());
    auto pf = perfect_filter<chd_hasher, 16>::build(std::move(*phf), keys);
    for (const auto& key : keys) {
        REQUIRE(pf.contains(key));
    }
}

// ===== BBHASH TESTS =====

TEST_CASE("BBHash: bijectivity", "[bbhash]") {
    SECTION("Small key set") {
        std::vector<std::string> keys = {"alpha", "beta", "gamma", "delta", "epsilon"};
        auto phf = bbhash_hasher<3>::builder{}.add_all(keys).with_gamma(2.0).build();
        REQUIRE(phf.has_value());
        REQUIRE(verify_bijectivity(*phf, keys));
        REQUIRE(phf->num_keys() == 5);
    }

    SECTION("1000 keys") {
        auto keys = make_keys(1000);
        auto phf = bbhash_hasher<3>::builder{}.add_all(keys).with_gamma(2.0).build();
        REQUIRE(phf.has_value());
        REQUIRE(verify_bijectivity(*phf, keys));
    }
}

TEST_CASE("BBHash: space efficiency", "[bbhash]") {
    auto keys = make_keys(10000);
    auto phf = bbhash_hasher<3>::builder{}.add_all(keys).with_gamma(2.0).build();
    REQUIRE(phf.has_value());
    INFO("BBHash bits/key: " << phf->bits_per_key());
    REQUIRE(phf->bits_per_key() > 0.0);
    REQUIRE(phf->bits_per_key() < 20.0);  // BBHash with rank checkpoints ~12-16 bits/key
}

TEST_CASE("BBHash: serialization round-trip", "[bbhash]") {
    auto keys = make_keys(500);
    auto phf = bbhash_hasher<3>::builder{}.add_all(keys).with_gamma(2.0).build();
    REQUIRE(phf.has_value());
    auto bytes = phf->serialize();
    auto restored = bbhash_hasher<3>::deserialize(bytes);
    REQUIRE(restored.has_value());
    for (const auto& key : keys) {
        REQUIRE(restored->slot_for(key).value == phf->slot_for(key).value);
    }
}

TEST_CASE("BBHash: works with perfect_filter", "[bbhash]") {
    auto keys = make_keys(500);
    auto phf = bbhash_hasher<3>::builder{}.add_all(keys).with_gamma(2.0).build();
    REQUIRE(phf.has_value());
    auto pf = perfect_filter<bbhash_hasher<3>, 16>::build(std::move(*phf), keys);
    for (const auto& key : keys) {
        REQUIRE(pf.contains(key));
    }
}

// ===== FCH TESTS =====

TEST_CASE("FCH: bijectivity", "[fch]") {
    SECTION("Small key set") {
        std::vector<std::string> keys;
        for (int i = 0; i < 15; ++i) keys.push_back("fch_key_" + std::to_string(i));
        auto phf = fch_hasher::builder{}.add_all(keys).build();
        REQUIRE(phf.has_value());
        REQUIRE(verify_bijectivity(*phf, keys));
    }

    SECTION("1000 keys") {
        auto keys = make_keys(1000);
        auto phf = fch_hasher::builder{}.add_all(keys).build();
        REQUIRE(phf.has_value());
        REQUIRE(verify_bijectivity(*phf, keys));
    }
}

TEST_CASE("FCH: space efficiency", "[fch]") {
    auto keys = make_keys(1000);
    auto phf = fch_hasher::builder{}.add_all(keys).build();
    REQUIRE(phf.has_value());
    INFO("FCH bits/key: " << phf->bits_per_key());
    REQUIRE(phf->bits_per_key() > 0.0);
    REQUIRE(phf->bits_per_key() < 250.0);  // FCH uses 3x table + displacements, ~200 bits/key
}

TEST_CASE("FCH: serialization round-trip", "[fch]") {
    auto keys = make_keys(500);
    auto phf = fch_hasher::builder{}.add_all(keys).build();
    REQUIRE(phf.has_value());
    auto bytes = phf->serialize();
    auto restored = fch_hasher::deserialize(bytes);
    REQUIRE(restored.has_value());
    for (const auto& key : keys) {
        REQUIRE(restored->slot_for(key).value == phf->slot_for(key).value);
    }
}

TEST_CASE("FCH: works with perfect_filter", "[fch]") {
    auto keys = make_keys(500);
    auto phf = fch_hasher::builder{}.add_all(keys).build();
    REQUIRE(phf.has_value());
    auto pf = perfect_filter<fch_hasher, 16>::build(std::move(*phf), keys);
    for (const auto& key : keys) {
        REQUIRE(pf.contains(key));
    }
}

// ===== PTHASH TESTS =====

TEST_CASE("PTHash: bijectivity", "[pthash]") {
    SECTION("Small key set") {
        std::vector<std::string> keys = {"pt_a", "pt_b", "pt_c", "pt_d", "pt_e"};
        auto phf = pthash_hasher<98>::builder{}.add_all(keys).build();
        REQUIRE(phf.has_value());
        REQUIRE(verify_bijectivity(*phf, keys));
    }

    SECTION("50 keys") {
        auto keys = make_keys(50);
        auto phf = pthash_hasher<98>::builder{}.add_all(keys).build();
        REQUIRE(phf.has_value());
        REQUIRE(verify_bijectivity(*phf, keys));
    }
}

TEST_CASE("PTHash: serialization round-trip", "[pthash]") {
    auto keys = make_keys(30);
    auto phf = pthash_hasher<98>::builder{}.add_all(keys).build();
    REQUIRE(phf.has_value());
    auto bytes = phf->serialize();
    auto restored = pthash_hasher<98>::deserialize(bytes);
    REQUIRE(restored.has_value());
    for (const auto& key : keys) {
        REQUIRE(restored->slot_for(key).value == phf->slot_for(key).value);
    }
}

// ===== CROSS-ALGORITHM COMPARISON =====

TEST_CASE("All algorithms: consistent bijectivity at 100 keys", "[comparison]") {
    auto keys = make_keys(100);

    auto rs = recsplit_hasher<8>::builder{}.add_all(keys).build();
    auto chd = chd_hasher::builder{}.add_all(keys).build();
    auto bb = bbhash_hasher<3>::builder{}.add_all(keys).with_gamma(2.0).build();
    auto fch = fch_hasher::builder{}.add_all(keys).build();

    REQUIRE(rs.has_value());
    REQUIRE(chd.has_value());
    REQUIRE(bb.has_value());
    REQUIRE(fch.has_value());

    REQUIRE(verify_bijectivity(*rs, keys));
    REQUIRE(verify_bijectivity(*chd, keys));
    REQUIRE(verify_bijectivity(*bb, keys));
    REQUIRE(verify_bijectivity(*fch, keys));
}
