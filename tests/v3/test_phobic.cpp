/**
 * @file test_phobic.cpp
 * @brief Tests for PHOBIC perfect hash function
 */

#include <catch2/catch_test_macros.hpp>
#include <maph/algorithms/phobic.hpp>
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

std::vector<std::string> make_sequential_keys(size_t count) {
    std::vector<std::string> keys;
    keys.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        keys.push_back("key_" + std::to_string(i));
    }
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

static_assert(perfect_hash_function<phobic_phf<5>>,
    "phobic_phf<5> must satisfy perfect_hash_function");

TEST_CASE("phobic: bijectivity small key sets", "[phobic]") {
    SECTION("3 keys") {
        std::vector<std::string> keys = {"alpha", "beta", "gamma"};
        auto phf = phobic_phf<5>::builder{}.add_all(keys).build();
        REQUIRE(phf.has_value());
        REQUIRE(verify_bijectivity(*phf, keys));
        REQUIRE(phf->num_keys() == 3);
        REQUIRE(phf->range_size() >= 3);
    }

    SECTION("10 keys") {
        auto keys = make_sequential_keys(10);
        auto phf = phobic_phf<5>::builder{}.add_all(keys).build();
        REQUIRE(phf.has_value());
        REQUIRE(verify_bijectivity(*phf, keys));
    }

    SECTION("100 keys") {
        auto keys = make_keys(100);
        auto phf = phobic_phf<5>::builder{}.add_all(keys).build();
        REQUIRE(phf.has_value());
        REQUIRE(verify_bijectivity(*phf, keys));
    }

    SECTION("Single key") {
        std::vector<std::string> keys = {"only"};
        auto phf = phobic_phf<5>::builder{}.add_all(keys).build();
        REQUIRE(phf.has_value());
        REQUIRE(verify_bijectivity(*phf, keys));
        REQUIRE(phf->num_keys() == 1);
    }
}

TEST_CASE("phobic: bijectivity medium key sets", "[phobic]") {
    SECTION("1K keys") {
        auto keys = make_keys(1000);
        auto phf = phobic_phf<5>::builder{}.add_all(keys).build();
        REQUIRE(phf.has_value());
        REQUIRE(verify_bijectivity(*phf, keys));
    }

    SECTION("10K keys") {
        auto keys = make_keys(10000);
        auto phf = phobic_phf<5>::builder{}.add_all(keys).build();
        REQUIRE(phf.has_value());
        REQUIRE(verify_bijectivity(*phf, keys));
    }
}

TEST_CASE("phobic: minimal mode (alpha=1.0)", "[phobic]") {
    auto keys = make_keys(1000);
    auto phf = phobic_phf<5>::builder{}
        .add_all(keys)
        .with_alpha(1.0)
        .build();
    REQUIRE(phf.has_value());
    REQUIRE(phf->range_size() == phf->num_keys());
    REQUIRE(verify_bijectivity(*phf, keys));
}

TEST_CASE("phobic: non-minimal mode (alpha=1.05)", "[phobic]") {
    auto keys = make_keys(1000);
    auto phf = phobic_phf<5>::builder{}
        .add_all(keys)
        .with_alpha(1.05)
        .build();
    REQUIRE(phf.has_value());
    REQUIRE(phf->range_size() > phf->num_keys());
    REQUIRE(verify_bijectivity(*phf, keys));
}

TEST_CASE("phobic: space efficiency", "[phobic]") {
    auto keys = make_keys(10000);
    auto phf = phobic_phf<5>::builder{}.add_all(keys).build();
    REQUIRE(phf.has_value());

    INFO("Bits per key: " << phf->bits_per_key());
    INFO("Memory bytes: " << phf->memory_bytes());
    REQUIRE(phf->bits_per_key() > 0.0);
    REQUIRE(phf->bits_per_key() < 3.0);
}

TEST_CASE("phobic: deterministic with same seed", "[phobic]") {
    auto keys = make_keys(500);

    auto phf1 = phobic_phf<5>::builder{}.add_all(keys).with_seed(12345).build();
    auto phf2 = phobic_phf<5>::builder{}.add_all(keys).with_seed(12345).build();
    REQUIRE(phf1.has_value());
    REQUIRE(phf2.has_value());

    for (const auto& key : keys) {
        REQUIRE(phf1->slot_for(key).value == phf2->slot_for(key).value);
    }
}

TEST_CASE("phobic: serialization round-trip", "[phobic]") {
    auto keys = make_keys(1000);
    auto phf = phobic_phf<5>::builder{}.add_all(keys).build();
    REQUIRE(phf.has_value());

    auto bytes = phf->serialize();
    REQUIRE(!bytes.empty());

    auto restored = phobic_phf<5>::deserialize(bytes);
    REQUIRE(restored.has_value());

    REQUIRE(restored->num_keys() == phf->num_keys());
    REQUIRE(restored->range_size() == phf->range_size());

    for (const auto& key : keys) {
        REQUIRE(restored->slot_for(key).value == phf->slot_for(key).value);
    }
}

TEST_CASE("phobic: builder add individual keys", "[phobic]") {
    auto b = phobic_phf<5>::builder{};
    b.add("one").add("two").add("three");
    auto phf = b.build();
    REQUIRE(phf.has_value());
    REQUIRE(phf->num_keys() == 3);
    REQUIRE(verify_bijectivity(*phf, {"one", "two", "three"}));
}

TEST_CASE("phobic: builder with_seed", "[phobic]") {
    auto keys = make_keys(100);
    auto phf = phobic_phf<5>::builder{}.add_all(keys).with_seed(42).build();
    REQUIRE(phf.has_value());
    REQUIRE(verify_bijectivity(*phf, keys));
}

TEST_CASE("phobic: bucket size 3", "[phobic]") {
    auto keys = make_keys(1000);
    auto phf = phobic_phf<3>::builder{}.add_all(keys).build();
    REQUIRE(phf.has_value());
    REQUIRE(verify_bijectivity(*phf, keys));
}

TEST_CASE("phobic: bucket size 7", "[phobic]") {
    auto keys = make_keys(1000);
    auto phf = phobic_phf<7>::builder{}.add_all(keys).build();
    REQUIRE(phf.has_value());
    REQUIRE(verify_bijectivity(*phf, keys));
}
