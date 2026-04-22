/**
 * @file test_partitioned.cpp
 * @brief Tests for partitioned_phf composition.
 */

#include <catch2/catch_test_macros.hpp>
#include <maph/composition/partitioned.hpp>
#include <maph/algorithms/phobic.hpp>
#include <random>
#include <set>
#include <algorithm>

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
        for (size_t j = 0; j < len; ++j) key += static_cast<char>(char_dist(rng));
        keys.push_back(std::move(key));
    }
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    return keys;
}

template<typename PHF>
bool verify_bijectivity(const PHF& phf, const std::vector<std::string>& keys) {
    std::set<uint64_t> seen;
    for (const auto& k : keys) {
        auto s = phf.slot_for(k);
        if (s.value >= phf.range_size()) return false;
        if (!seen.insert(s.value).second) return false;
    }
    return seen.size() == keys.size();
}

}  // namespace

static_assert(perfect_hash_function<partitioned_phf<phobic5>>,
    "partitioned_phf<phobic5> must satisfy perfect_hash_function");

TEST_CASE("partitioned: bijectivity with phobic5 inner", "[partitioned]") {
    auto keys = make_keys(10000);
    auto phf = partitioned_phf<phobic5>::builder{}
        .add_all(keys).with_shards(8).build();
    REQUIRE(phf.has_value());
    REQUIRE(verify_bijectivity(*phf, keys));
    REQUIRE(phf->num_keys() == keys.size());
}

TEST_CASE("partitioned: bijectivity across inner variants", "[partitioned]") {
    auto keys = make_keys(5000);
    auto p3 = partitioned_phf<phobic3>::builder{}.add_all(keys).with_shards(4).build();
    auto p4 = partitioned_phf<phobic4>::builder{}.add_all(keys).with_shards(4).build();
    auto p5 = partitioned_phf<phobic5>::builder{}.add_all(keys).with_shards(4).build();
    REQUIRE(p3.has_value());
    REQUIRE(p4.has_value());
    REQUIRE(p5.has_value());
    REQUIRE(verify_bijectivity(*p3, keys));
    REQUIRE(verify_bijectivity(*p4, keys));
    REQUIRE(verify_bijectivity(*p5, keys));
}

TEST_CASE("partitioned: auto shard count", "[partitioned]") {
    auto keys = make_keys(50000);
    auto phf = partitioned_phf<phobic5>::builder{}.add_all(keys).with_shards(0).build();
    REQUIRE(phf.has_value());
    REQUIRE(verify_bijectivity(*phf, keys));
}

TEST_CASE("partitioned: serialization round-trip", "[partitioned]") {
    auto keys = make_keys(5000);
    auto phf = partitioned_phf<phobic5>::builder{}
        .add_all(keys).with_shards(8).with_seed(42).build();
    REQUIRE(phf.has_value());

    auto bytes = phf->serialize();
    auto restored = partitioned_phf<phobic5>::deserialize(bytes);
    REQUIRE(restored.has_value());
    REQUIRE(restored->num_keys() == phf->num_keys());
    REQUIRE(restored->range_size() == phf->range_size());

    for (const auto& k : keys) {
        REQUIRE(phf->slot_for(k).value == restored->slot_for(k).value);
    }
}

TEST_CASE("partitioned: determinism with same seed", "[partitioned]") {
    auto keys = make_keys(3000);
    auto a = partitioned_phf<phobic5>::builder{}
        .add_all(keys).with_shards(4).with_seed(7).build();
    auto b = partitioned_phf<phobic5>::builder{}
        .add_all(keys).with_shards(4).with_seed(7).build();
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    for (const auto& k : keys) {
        REQUIRE(a->slot_for(k).value == b->slot_for(k).value);
    }
}

TEST_CASE("partitioned: explicit threads=1 works same as threads=0", "[partitioned]") {
    auto keys = make_keys(3000);
    auto a = partitioned_phf<phobic5>::builder{}
        .add_all(keys).with_shards(4).with_seed(7).with_threads(1).build();
    auto b = partitioned_phf<phobic5>::builder{}
        .add_all(keys).with_shards(4).with_seed(7).with_threads(0).build();
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    // Order-of-build doesn't affect result; per-shard seeds are deterministic.
    for (const auto& k : keys) {
        REQUIRE(a->slot_for(k).value == b->slot_for(k).value);
    }
}

TEST_CASE("partitioned: single shard degenerate case", "[partitioned]") {
    auto keys = make_keys(2000);
    auto phf = partitioned_phf<phobic5>::builder{}
        .add_all(keys).with_shards(1).build();
    REQUIRE(phf.has_value());
    REQUIRE(verify_bijectivity(*phf, keys));
}
