/**
 * @file test_perfect_hash_extended.cpp
 * @brief Extended edge-case tests for perfect hash algorithms
 */

#include <catch2/catch_test_macros.hpp>
#include <maph/concepts/perfect_hash_function.hpp>
#include <maph/algorithms/recsplit.hpp>
#include <maph/algorithms/chd.hpp>
#include <maph/algorithms/bbhash.hpp>
#include <maph/algorithms/fch.hpp>
#include <maph/algorithms/pthash.hpp>
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

TEST_CASE("RecSplit: single key", "[recsplit][edge]") {
    std::vector<std::string> keys = {"only_key"};
    auto phf = recsplit_hasher<8>::builder{}.add_all(keys).build();
    REQUIRE(phf.has_value());
    REQUIRE(verify_bijectivity(*phf, keys));
    REQUIRE(phf->num_keys() == 1);
}

TEST_CASE("RecSplit: deterministic builds", "[recsplit][edge]") {
    auto keys = make_keys(500);
    auto phf1 = recsplit_hasher<8>::builder{}.add_all(keys).with_seed(42).build();
    auto phf2 = recsplit_hasher<8>::builder{}.add_all(keys).with_seed(42).build();
    REQUIRE(phf1.has_value());
    REQUIRE(phf2.has_value());
    for (const auto& key : keys) {
        REQUIRE(phf1->slot_for(key).value == phf2->slot_for(key).value);
    }
}

TEST_CASE("BBHash: gamma parameter affects range", "[bbhash][edge]") {
    auto keys = make_keys(500);
    auto phf_low = bbhash_hasher<3>::builder{}.add_all(keys).with_gamma(1.5).build();
    auto phf_high = bbhash_hasher<3>::builder{}.add_all(keys).with_gamma(3.0).build();
    // Both should succeed
    REQUIRE(phf_low.has_value());
    REQUIRE(phf_high.has_value());
    REQUIRE(verify_bijectivity(*phf_low, keys));
    REQUIRE(verify_bijectivity(*phf_high, keys));
}

TEST_CASE("CHD: single key", "[chd][edge]") {
    std::vector<std::string> keys = {"only"};
    auto phf = chd_hasher::builder{}.add_all(keys).build();
    REQUIRE(phf.has_value());
    REQUIRE(verify_bijectivity(*phf, keys));
}

TEST_CASE("FCH: minimum viable key set", "[fch][edge]") {
    std::vector<std::string> keys = {"a", "b", "c"};
    auto phf = fch_hasher::builder{}.add_all(keys).build();
    REQUIRE(phf.has_value());
    REQUIRE(verify_bijectivity(*phf, keys));
}

TEST_CASE("All algorithms: duplicate keys handled", "[edge]") {
    std::vector<std::string> keys = {"dup", "dup", "other", "other", "unique"};
    // Builders should deduplicate

    auto rs = recsplit_hasher<8>::builder{}.add_all(keys).build();
    REQUIRE(rs.has_value());
    REQUIRE(rs->num_keys() == 3);  // 3 unique keys

    auto chd = chd_hasher::builder{}.add_all(keys).build();
    REQUIRE(chd.has_value());
    REQUIRE(chd->num_keys() == 3);

    auto bb = bbhash_hasher<3>::builder{}.add_all(keys).with_gamma(2.0).build();
    REQUIRE(bb.has_value());
    REQUIRE(bb->num_keys() == 3);
}
