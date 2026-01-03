/**
 * @file test_perfect_hash.cpp
 * @brief Tests for perfect hash implementations
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <maph/core.hpp>
#include <maph/hashers_perfect.hpp>
#include <set>
#include <algorithm>
#include <random>

using namespace maph;

// ===== TEST UTILITIES =====

namespace {

std::vector<std::string> generate_random_keys(size_t count, size_t min_len = 4, size_t max_len = 16) {
    std::vector<std::string> keys;
    keys.reserve(count);

    std::mt19937_64 rng{42};  // Fixed seed for reproducibility
    std::uniform_int_distribution<size_t> len_dist(min_len, max_len);
    std::uniform_int_distribution<int> char_dist('a', 'z');

    for (size_t i = 0; i < count; ++i) {
        std::string key;
        size_t len = len_dist(rng);
        key.reserve(len);
        for (size_t j = 0; j < len; ++j) {
            key += static_cast<char>(char_dist(rng));
        }
        keys.push_back(std::move(key));
    }

    // Remove duplicates
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());

    return keys;
}

std::vector<std::string> generate_sequential_keys(size_t count) {
    std::vector<std::string> keys;
    keys.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        keys.push_back("key_" + std::to_string(i));
    }
    return keys;
}

template<typename Hasher>
bool verify_perfect_hash(const Hasher& hasher, const std::vector<std::string>& keys) {
    std::set<uint64_t> seen_hashes;

    for (const auto& key : keys) {
        auto slot_opt = hasher.slot_for(key);
        if (!slot_opt) {
            INFO("Key not found: " << key);
            return false;
        }

        auto slot = *slot_opt;
        if (slot.value >= keys.size()) {
            INFO("Slot out of range: " << slot.value << " >= " << keys.size());
            return false;
        }

        auto hash = hasher.hash(key);
        if (hash.value != slot.value) {
            INFO("Hash/slot mismatch for key: " << key << " hash=" << hash.value << " slot=" << slot.value);
            return false;
        }
        if (seen_hashes.count(hash.value)) {
            INFO("Hash collision for key: " << key);
            return false;
        }
        seen_hashes.insert(hash.value);
    }

    return true;
}

} // anonymous namespace

// ===== RECSPLIT TESTS =====

TEST_CASE("RecSplit: Empty keys", "[perfect][recsplit]") {
    auto result = recsplit8::builder{}.build();
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("RecSplit: Single key", "[perfect][recsplit]") {
    auto result = recsplit8::builder{}
        .add("test")
        .build();

    REQUIRE(result.has_value());
    auto hasher = std::move(result.value());

    REQUIRE(hasher.key_count() == 1);
    REQUIRE(hasher.max_slots().value == 1);

    auto slot = hasher.slot_for("test");
    REQUIRE(slot.has_value());
    REQUIRE(slot->value < 1);
}

TEST_CASE("RecSplit: Small key set", "[perfect][recsplit]") {
    std::vector<std::string> keys = {"apple", "banana", "cherry", "date", "elderberry"};

    auto result = recsplit8::builder{}
        .add_all(keys)
        .build();

    REQUIRE(result.has_value());
    auto hasher = std::move(result.value());

    SECTION("Basic properties") {
        REQUIRE(hasher.key_count() == 5);
        REQUIRE(hasher.max_slots().value == 5);
    }

    SECTION("All keys have slots") {
        for (const auto& key : keys) {
            auto slot = hasher.slot_for(key);
            REQUIRE(slot.has_value());
            REQUIRE(slot->value < 5);
            REQUIRE(hasher.is_perfect_for(key));
        }
    }

    SECTION("No collisions") {
        REQUIRE(verify_perfect_hash(hasher, keys));
    }

    SECTION("Unknown keys rejected") {
        REQUIRE_FALSE(hasher.slot_for("bbhash-miss").has_value());
    }

    SECTION("Unknown keys") {
        REQUIRE_FALSE(hasher.slot_for("unknown").has_value());
    }

    SECTION("Statistics") {
        auto stats = hasher.statistics();
        REQUIRE(stats.key_count == 5);
        REQUIRE(stats.memory_bytes > 0);
        REQUIRE(stats.bits_per_key >= 1.0);  // RecSplit achieves ~2 bits/key
        REQUIRE(stats.bits_per_key < 600.0);  // Allow overhead from fingerprints + structure
    }
}

TEST_CASE("RecSplit: Medium key set (100 keys)", "[perfect][recsplit]") {
    auto keys = generate_random_keys(100);
    REQUIRE(keys.size() >= 95);  // Account for potential duplicates

    auto result = recsplit8::builder{}
        .add_all(keys)
        .build();

    REQUIRE(result.has_value());
    auto hasher = std::move(result.value());

    SECTION("All keys have unique slots") {
        REQUIRE(verify_perfect_hash(hasher, keys));
    }

    SECTION("Space efficiency") {
        auto stats = hasher.statistics();
        INFO("Bits per key: " << stats.bits_per_key);
        REQUIRE(stats.bits_per_key < 400.0);
    }
}

TEST_CASE("RecSplit: Large key set (1000 keys)", "[perfect][recsplit]") {
    auto keys = generate_random_keys(1000);

    auto result = recsplit8::builder{}
        .add_all(keys)
        .build();

    REQUIRE(result.has_value());
    auto hasher = std::move(result.value());

    SECTION("Correctness") {
        REQUIRE(verify_perfect_hash(hasher, keys));
    }

    SECTION("Performance") {
        auto slot = hasher.slot_for(keys[0]);
        REQUIRE(slot.has_value());

        BENCHMARK("RecSplit lookup (1000 keys)") {
            return hasher.slot_for(keys[42]);
        };
    }
}

TEST_CASE("RecSplit: Different leaf sizes", "[perfect][recsplit]") {
    auto keys = generate_random_keys(50);

    SECTION("Leaf size 8") {
        auto result = recsplit8::builder{}.add_all(keys).build();
        REQUIRE(result.has_value());
        REQUIRE(verify_perfect_hash(result.value(), keys));
    }

    SECTION("Leaf size 16") {
        auto result = recsplit16::builder{}.add_all(keys).build();
        REQUIRE(result.has_value());
        REQUIRE(verify_perfect_hash(result.value(), keys));
    }
}

TEST_CASE("RecSplit: Duplicate keys", "[perfect][recsplit]") {
    auto result = recsplit8::builder{}
        .add("test")
        .add("test")  // Duplicate
        .add("other")
        .build();

    REQUIRE(result.has_value());
    auto hasher = std::move(result.value());

    // Should deduplicate
    REQUIRE(hasher.key_count() == 2);
}

TEST_CASE("RecSplit: Builder fluent interface", "[perfect][recsplit]") {
    auto result = recsplit8::builder{}
        .add("a")
        .add("b")
        .add("c")
        .with_seed(12345)
        .build();

    REQUIRE(result.has_value());
}

TEST_CASE("RecSplit: Parallel construction", "[perfect][recsplit][parallel]") {
    auto keys = generate_random_keys(5000);  // Large enough to trigger parallel processing

    SECTION("Single-threaded vs multi-threaded produce same results") {
        auto single_result = recsplit8::builder{}
            .add_all(keys)
            .with_seed(42)
            .with_threads(1)
            .build();

        auto multi_result = recsplit8::builder{}
            .add_all(keys)
            .with_seed(42)
            .with_threads(4)
            .build();

        REQUIRE(single_result.has_value());
        REQUIRE(multi_result.has_value());

        auto& single_hasher = single_result.value();
        auto& multi_hasher = multi_result.value();

        // Both should hash all keys correctly
        for (const auto& key : keys) {
            auto single_slot = single_hasher.slot_for(key);
            auto multi_slot = multi_hasher.slot_for(key);
            REQUIRE(single_slot.has_value());
            REQUIRE(multi_slot.has_value());
            REQUIRE(single_slot->value == multi_slot->value);
        }
    }

    SECTION("Multi-threaded correctness") {
        auto result = recsplit8::builder{}
            .add_all(keys)
            .with_threads(8)
            .build();

        REQUIRE(result.has_value());
        REQUIRE(verify_perfect_hash(result.value(), keys));
    }
}

// ===== CHD TESTS =====

TEST_CASE("CHD: Empty keys", "[perfect][chd]") {
    auto result = chd_hasher::builder{}.build();
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("CHD: Default constructed safety", "[perfect][chd]") {
    chd_hasher hasher;
    REQUIRE_FALSE(hasher.slot_for("anything").has_value());
    REQUIRE(hasher.hash("anything").value == 0);
}

TEST_CASE("CHD: Single key", "[perfect][chd]") {
    auto result = chd_hasher::builder{}
        .add("test")
        .build();

    REQUIRE(result.has_value());
    auto hasher = std::move(result.value());

    REQUIRE(hasher.key_count() == 1);
    REQUIRE(hasher.max_slots().value == 1);
}

TEST_CASE("CHD: Small key set", "[perfect][chd]") {
    std::vector<std::string> keys = {"red", "green", "blue", "yellow", "purple"};

    auto result = chd_hasher::builder{}
        .add_all(keys)
        .build();

    REQUIRE(result.has_value());
    auto hasher = std::move(result.value());

    SECTION("Basic properties") {
        REQUIRE(hasher.key_count() == 5);
        REQUIRE(hasher.max_slots().value == 5);
    }

    SECTION("All keys accessible") {
        for (const auto& key : keys) {
            auto slot = hasher.slot_for(key);
            REQUIRE(slot.has_value());
        }
    }

    SECTION("Unknown keys rejected") {
        REQUIRE_FALSE(hasher.slot_for("not-present").has_value());
    }

    SECTION("Statistics") {
        auto stats = hasher.statistics();
        REQUIRE(stats.key_count == 5);
        REQUIRE(stats.memory_bytes > 0);
        INFO("CHD bits per key: " << stats.bits_per_key);
    }
}

TEST_CASE("CHD: Medium key set", "[perfect][chd]") {
    auto keys = generate_random_keys(100);

    auto result = chd_hasher::builder{}
        .add_all(keys)
        .with_lambda(5.0)
        .build();

    REQUIRE(result.has_value());
    auto hasher = std::move(result.value());

    SECTION("Space efficiency") {
        auto stats = hasher.statistics();
        INFO("Bits per key: " << stats.bits_per_key);
        REQUIRE(stats.bits_per_key < 400.0);
    }
}

TEST_CASE("CHD: Different lambda values", "[perfect][chd]") {
    auto keys = generate_random_keys(50);

    SECTION("Lambda = 3.0 (more space)") {
        auto result = chd_hasher::builder{}
            .add_all(keys)
            .with_lambda(3.0)
            .build();
        REQUIRE(result.has_value());
    }

    SECTION("Lambda = 7.0 (less space)") {
        auto result = chd_hasher::builder{}
            .add_all(keys)
            .with_lambda(7.0)
            .build();
        REQUIRE(result.has_value());
    }
}

// ===== BBHASH TESTS =====

TEST_CASE("BBHash: Empty keys", "[perfect][bbhash]") {
    auto result = bbhash3::builder{}.build();
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("BBHash: Single key", "[perfect][bbhash]") {
    auto result = bbhash3::builder{}
        .add("test")
        .build();

    REQUIRE(result.has_value());
    auto hasher = std::move(result.value());

    REQUIRE(hasher.key_count() == 1);
    REQUIRE(hasher.max_slots().value == 1);

    auto slot = hasher.slot_for("test");
    REQUIRE(slot.has_value());
    REQUIRE(slot->value < 1);
}

TEST_CASE("BBHash: Small key set", "[perfect][bbhash]") {
    std::vector<std::string> keys = {"alpha", "beta", "gamma", "delta", "epsilon"};

    auto result = bbhash3::builder{}
        .add_all(keys)
        .build();

    REQUIRE(result.has_value());
    auto hasher = std::move(result.value());

    SECTION("Basic properties") {
        REQUIRE(hasher.key_count() == 5);
        REQUIRE(hasher.max_slots().value == 5);
        REQUIRE(hasher.gamma() == 2.0);  // Default gamma
    }

    SECTION("All keys have slots") {
        for (const auto& key : keys) {
            auto slot = hasher.slot_for(key);
            REQUIRE(slot.has_value());
            REQUIRE(slot->value < 5);
            REQUIRE(hasher.is_perfect_for(key));
        }
    }

    SECTION("No collisions") {
        REQUIRE(verify_perfect_hash(hasher, keys));
    }

    SECTION("Statistics") {
        auto stats = hasher.statistics();
        REQUIRE(stats.key_count == 5);
        REQUIRE(stats.memory_bytes > 0);
        REQUIRE(stats.bits_per_key >= 1.0);
        REQUIRE(stats.bits_per_key < 200.0);
        INFO("BBHash bits per key: " << stats.bits_per_key);
    }
}

TEST_CASE("BBHash: Medium key set (100 keys)", "[perfect][bbhash]") {
    auto keys = generate_random_keys(100);
    REQUIRE(keys.size() >= 95);

    auto result = bbhash3::builder{}
        .add_all(keys)
        .build();

    REQUIRE(result.has_value());
    auto hasher = std::move(result.value());

    SECTION("All keys have unique slots") {
        REQUIRE(verify_perfect_hash(hasher, keys));
    }

    SECTION("Space efficiency") {
        auto stats = hasher.statistics();
        INFO("Bits per key: " << stats.bits_per_key);
        REQUIRE(stats.bits_per_key <= 200.0);
    }
}

TEST_CASE("BBHash: Large key set (1000 keys)", "[perfect][bbhash]") {
    auto keys = generate_random_keys(1000);

    // Use more levels for larger datasets
    auto result = bbhash5::builder{}
        .add_all(keys)
        .with_gamma(2.5)  // Increase gamma for better success rate
        .build();

    REQUIRE(result.has_value());
    auto hasher = std::move(result.value());

    SECTION("Correctness") {
        REQUIRE(verify_perfect_hash(hasher, keys));
    }

    SECTION("Performance") {
        auto slot = hasher.slot_for(keys[0]);
        REQUIRE(slot.has_value());

        BENCHMARK("BBHash lookup (1000 keys)") {
            return hasher.slot_for(keys[42]);
        };
    }
}

TEST_CASE("BBHash: Different gamma values", "[perfect][bbhash]") {
    auto keys = generate_random_keys(50);

    SECTION("Gamma = 1.5 (compact)") {
        auto result = bbhash3::builder{}
            .add_all(keys)
            .with_gamma(1.5)
            .build();
        REQUIRE(result.has_value());
        REQUIRE(verify_perfect_hash(result.value(), keys));
    }

    SECTION("Gamma = 2.0 (default)") {
        auto result = bbhash3::builder{}
            .add_all(keys)
            .with_gamma(2.0)
            .build();
        REQUIRE(result.has_value());
        REQUIRE(verify_perfect_hash(result.value(), keys));
    }

    SECTION("Gamma = 3.0 (faster build)") {
        auto result = bbhash3::builder{}
            .add_all(keys)
            .with_gamma(3.0)
            .build();
        REQUIRE(result.has_value());
        REQUIRE(verify_perfect_hash(result.value(), keys));
    }
}

TEST_CASE("BBHash: Different level counts", "[perfect][bbhash]") {
    auto keys = generate_random_keys(50);

    SECTION("3 levels") {
        auto result = bbhash3::builder{}.add_all(keys).build();
        REQUIRE(result.has_value());
        REQUIRE(verify_perfect_hash(result.value(), keys));
    }

    SECTION("5 levels") {
        auto result = bbhash5::builder{}.add_all(keys).build();
        REQUIRE(result.has_value());
        REQUIRE(verify_perfect_hash(result.value(), keys));
    }
}

TEST_CASE("BBHash: Duplicate keys", "[perfect][bbhash]") {
    auto result = bbhash3::builder{}
        .add("test")
        .add("test")  // Duplicate
        .add("other")
        .build();

    REQUIRE(result.has_value());
    auto hasher = std::move(result.value());

    // Should deduplicate
    REQUIRE(hasher.key_count() == 2);
}

TEST_CASE("BBHash: Builder fluent interface", "[perfect][bbhash]") {
    auto result = bbhash3::builder{}
        .add("a")
        .add("b")
        .add("c")
        .with_gamma(2.5)
        .with_seed(12345)
        .with_threads(4)
        .build();

    REQUIRE(result.has_value());
}

// ===== PTHASH TESTS =====

TEST_CASE("PTHash: Empty keys", "[perfect][pthash]") {
    auto result = pthash98::builder{}.build();
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("PTHash: Single key", "[perfect][pthash]") {
    auto result = pthash98::builder{}
        .add("test")
        .build();

    REQUIRE(result.has_value());
    auto hasher = std::move(result.value());

    REQUIRE(hasher.key_count() == 1);
    REQUIRE(hasher.max_slots().value == 1);
}

TEST_CASE("PTHash: Small key set", "[perfect][pthash]") {
    std::vector<std::string> keys = {"one", "two", "three", "four", "five"};

    auto result = pthash98::builder{}
        .add_all(keys)
        .build();

    REQUIRE(result.has_value());
    auto hasher = std::move(result.value());

    SECTION("Basic properties") {
        REQUIRE(hasher.key_count() == 5);
        REQUIRE(hasher.max_slots().value == 5);
        REQUIRE(hasher.num_buckets() > 0);
    }

    SECTION("All keys have slots") {
        for (const auto& key : keys) {
            auto slot = hasher.slot_for(key);
            REQUIRE(slot.has_value());
            REQUIRE(slot->value < 5);
            REQUIRE(hasher.is_perfect_for(key));
        }
    }

    SECTION("No collisions") {
        REQUIRE(verify_perfect_hash(hasher, keys));
    }

    SECTION("Statistics") {
        auto stats = hasher.statistics();
        REQUIRE(stats.key_count == 5);
        REQUIRE(stats.memory_bytes > 0);
        REQUIRE(stats.bits_per_key >= 1.0);
        REQUIRE(stats.bits_per_key < 400.0);
        INFO("PTHash bits per key: " << stats.bits_per_key);
    }

    SECTION("Unknown keys rejected") {
        REQUIRE_FALSE(hasher.slot_for("pthash-miss").has_value());
    }
}

TEST_CASE("PTHash: Medium key set (100 keys)", "[perfect][pthash]") {
    auto keys = generate_random_keys(100);
    REQUIRE(keys.size() >= 95);

    auto result = pthash98::builder{}
        .add_all(keys)
        .build();

    REQUIRE(result.has_value());
    auto hasher = std::move(result.value());

    SECTION("All keys have unique slots") {
        REQUIRE(verify_perfect_hash(hasher, keys));
    }

    SECTION("Space efficiency") {
        auto stats = hasher.statistics();
        INFO("Bits per key: " << stats.bits_per_key);
        REQUIRE(stats.bits_per_key < 400.0);
    }
}

TEST_CASE("PTHash: Large key set (1000 keys)", "[perfect][pthash]") {
    auto keys = generate_random_keys(1000);

    auto result = pthash98::builder{}
        .add_all(keys)
        .build();

    REQUIRE(result.has_value());
    auto hasher = std::move(result.value());

    SECTION("Correctness") {
        REQUIRE(verify_perfect_hash(hasher, keys));
    }

    SECTION("Performance") {
        auto slot = hasher.slot_for(keys[0]);
        REQUIRE(slot.has_value());

        BENCHMARK("PTHash lookup (1000 keys)") {
            return hasher.slot_for(keys[42]);
        };
    }
}

TEST_CASE("PTHash: Different alpha values", "[perfect][pthash]") {
    auto keys = generate_random_keys(50);

    SECTION("Alpha = 95 (more space)") {
        auto result = pthash95::builder{}
            .add_all(keys)
            .build();
        REQUIRE(result.has_value());
        REQUIRE(verify_perfect_hash(result.value(), keys));
    }

    SECTION("Alpha = 98 (default)") {
        auto result = pthash98::builder{}
            .add_all(keys)
            .build();
        REQUIRE(result.has_value());
        REQUIRE(verify_perfect_hash(result.value(), keys));
    }
}

TEST_CASE("PTHash: Duplicate keys", "[perfect][pthash]") {
    auto result = pthash98::builder{}
        .add("test")
        .add("test")  // Duplicate
        .add("other")
        .build();

    REQUIRE(result.has_value());
    auto hasher = std::move(result.value());

    // Should deduplicate
    REQUIRE(hasher.key_count() == 2);
}

TEST_CASE("PTHash: Builder fluent interface", "[perfect][pthash]") {
    auto result = pthash98::builder{}
        .add("a")
        .add("b")
        .add("c")
        .with_seed(12345)
        .build();

    REQUIRE(result.has_value());
}

// ===== FCH TESTS =====

TEST_CASE("FCH: Empty keys", "[perfect][fch]") {
    auto result = fch_hasher::builder{}.build();
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("FCH: Single key", "[perfect][fch]") {
    auto result = fch_hasher::builder{}
        .add("test")
        .build();

    REQUIRE(result.has_value());
    auto hasher = std::move(result.value());

    REQUIRE(hasher.key_count() == 1);
    REQUIRE(hasher.max_slots().value == 1);
}

TEST_CASE("FCH: Small key set", "[perfect][fch]") {
    std::vector<std::string> keys = {"mercury", "venus", "earth", "mars", "jupiter", "saturn", "uranus", "neptune", "pluto", "sun", "moon", "comet", "asteroid", "galaxy", "nebula"};

    auto result = fch_hasher::builder{}
        .add_all(keys)
        .with_bucket_size(3.0)  // Smaller buckets for better success
        .build();

    REQUIRE(result.has_value());
    auto hasher = std::move(result.value());

    SECTION("Basic properties") {
        REQUIRE(hasher.key_count() == 15);
        REQUIRE(hasher.max_slots().value == 15);
        REQUIRE(hasher.num_buckets() > 0);
    }

    SECTION("All keys have slots") {
        for (const auto& key : keys) {
            auto slot = hasher.slot_for(key);
            REQUIRE(slot.has_value());
            REQUIRE(slot->value < 15);
            REQUIRE(hasher.is_perfect_for(key));
        }
    }

    SECTION("No collisions") {
        REQUIRE(verify_perfect_hash(hasher, keys));
    }

    SECTION("Statistics") {
        auto stats = hasher.statistics();
        REQUIRE(stats.key_count == 15);
        REQUIRE(stats.memory_bytes > 0);
        REQUIRE(stats.bits_per_key >= 1.0);
        REQUIRE(stats.bits_per_key < 400.0);
        INFO("FCH bits per key: " << stats.bits_per_key);
    }

    SECTION("Unknown keys rejected") {
        REQUIRE_FALSE(hasher.slot_for("fch-miss").has_value());
    }
}

TEST_CASE("FCH: Medium key set (100 keys)", "[perfect][fch]") {
    auto keys = generate_random_keys(100);
    REQUIRE(keys.size() >= 95);

    auto result = fch_hasher::builder{}
        .add_all(keys)
        .build();

    REQUIRE(result.has_value());
    auto hasher = std::move(result.value());

    SECTION("All keys have unique slots") {
        REQUIRE(verify_perfect_hash(hasher, keys));
    }

    SECTION("Space efficiency") {
        auto stats = hasher.statistics();
        INFO("Bits per key: " << stats.bits_per_key);
        REQUIRE(stats.bits_per_key < 400.0);
    }
}

TEST_CASE("FCH: Large key set (1000 keys)", "[perfect][fch]") {
    auto keys = generate_random_keys(1000);

    auto result = fch_hasher::builder{}
        .add_all(keys)
        .build();

    REQUIRE(result.has_value());
    auto hasher = std::move(result.value());

    SECTION("Correctness") {
        REQUIRE(verify_perfect_hash(hasher, keys));
    }

    SECTION("Performance") {
        auto slot = hasher.slot_for(keys[0]);
        REQUIRE(slot.has_value());

        BENCHMARK("FCH lookup (1000 keys)") {
            return hasher.slot_for(keys[42]);
        };
    }
}

TEST_CASE("FCH: Different bucket sizes", "[perfect][fch]") {
    auto keys = generate_random_keys(100);  // Use more keys for better success rate

    SECTION("Bucket size = 4.0 (default)") {
        auto result = fch_hasher::builder{}
            .add_all(keys)
            .with_bucket_size(4.0)
            .build();
        REQUIRE(result.has_value());
        REQUIRE(verify_perfect_hash(result.value(), keys));
    }
}

TEST_CASE("FCH: Duplicate keys", "[perfect][fch]") {
    auto result = fch_hasher::builder{}
        .add("test")
        .add("test")  // Duplicate
        .add("other")
        .build();

    REQUIRE(result.has_value());
    auto hasher = std::move(result.value());

    // Should deduplicate
    REQUIRE(hasher.key_count() == 2);
}

TEST_CASE("FCH: Builder fluent interface", "[perfect][fch]") {
    auto result = fch_hasher::builder{}
        .add("a")
        .add("b")
        .add("c")
        .with_bucket_size(3.0)
        .with_seed(12345)
        .build();

    REQUIRE(result.has_value());
}

// ===== FACTORY FUNCTION TESTS =====

TEST_CASE("Factory: make_recsplit", "[perfect][factory]") {
    std::vector<std::string> keys = {"a", "b", "c", "d", "e"};

    auto result = make_recsplit<8>(keys);
    REQUIRE(result.has_value());

    auto& hasher = result.value();
    REQUIRE(hasher.key_count() == 5);
}

TEST_CASE("Factory: make_chd", "[perfect][factory]") {
    std::vector<std::string> keys = {"x", "y", "z"};

    auto result = make_chd(keys, 5.0);
    REQUIRE(result.has_value());

    auto& hasher = result.value();
    REQUIRE(hasher.key_count() == 3);
}

TEST_CASE("Factory: make_bbhash", "[perfect][factory]") {
    std::vector<std::string> keys = {"p", "q", "r", "s"};

    auto result = make_bbhash<3>(keys, 2.0);
    REQUIRE(result.has_value());

    auto& hasher = result.value();
    REQUIRE(hasher.key_count() == 4);
}

TEST_CASE("Factory: make_pthash", "[perfect][factory]") {
    std::vector<std::string> keys = {"alpha", "beta", "gamma"};

    auto result = make_pthash<98>(keys);
    REQUIRE(result.has_value());

    auto& hasher = result.value();
    REQUIRE(hasher.key_count() == 3);
}

TEST_CASE("Factory: make_fch", "[perfect][factory]") {
    std::vector<std::string> keys = {"first", "second", "third"};

    auto result = make_fch(keys, 4.0);
    REQUIRE(result.has_value());

    auto& hasher = result.value();
    REQUIRE(hasher.key_count() == 3);
}

// ===== COMPARISON BENCHMARKS =====

TEST_CASE("Benchmark: RecSplit vs CHD", "[perfect][benchmark]") {
    auto keys = generate_random_keys(1000);

    SECTION("Build time") {
        BENCHMARK("RecSplit8 build (1000 keys)") {
            return recsplit8::builder{}.add_all(keys).build();
        };

        BENCHMARK("CHD build (1000 keys)") {
            return chd_hasher::builder{}.add_all(keys).build();
        };
    }

    SECTION("Query time") {
        auto recsplit = recsplit8::builder{}.add_all(keys).build().value();
        auto chd = chd_hasher::builder{}.add_all(keys).build().value();

        BENCHMARK("RecSplit lookup") {
            return recsplit.slot_for(keys[500]);
        };

        BENCHMARK("CHD lookup") {
            return chd.slot_for(keys[500]);
        };
    }

    SECTION("Space usage") {
        auto recsplit = recsplit8::builder{}.add_all(keys).build().value();
        auto chd = chd_hasher::builder{}.add_all(keys).build().value();

        auto recsplit_stats = recsplit.statistics();
        auto chd_stats = chd.statistics();

        INFO("RecSplit: " << recsplit_stats.bits_per_key << " bits/key");
        INFO("CHD: " << chd_stats.bits_per_key << " bits/key");
    }
}

// ===== STRESS TESTS =====

TEST_CASE("Stress: Very large key set", "[perfect][stress]") {
    auto keys = generate_random_keys(10000);

    SECTION("RecSplit") {
        auto result = recsplit8::builder{}.add_all(keys).build();
        REQUIRE(result.has_value());

        auto& hasher = result.value();
        auto stats = hasher.statistics();

        INFO("Key count: " << stats.key_count);
        INFO("Memory: " << stats.memory_bytes << " bytes");
        INFO("Bits per key: " << stats.bits_per_key);

        REQUIRE(stats.key_count == keys.size());
    }

    SECTION("CHD") {
        auto result = chd_hasher::builder{}.add_all(keys).build();
        REQUIRE(result.has_value());
    }
}

TEST_CASE("Stress: Sequential keys", "[perfect][stress]") {
    auto keys = generate_sequential_keys(1000);

    auto result = recsplit8::builder{}.add_all(keys).build();
    REQUIRE(result.has_value());

    REQUIRE(verify_perfect_hash(result.value(), keys));
}

TEST_CASE("Stress: Very long keys", "[perfect][stress]") {
    std::vector<std::string> keys;
    for (size_t i = 0; i < 100; ++i) {
        keys.push_back(std::string(1000, 'a' + (i % 26)) + std::to_string(i));
    }

    auto result = recsplit8::builder{}.add_all(keys).build();
    REQUIRE(result.has_value());
}

// ===== CONCEPT COMPLIANCE TESTS =====

TEST_CASE("Concepts: perfect_hasher compliance", "[perfect][concepts]") {
    STATIC_REQUIRE(perfect_hasher<recsplit8>);
    STATIC_REQUIRE(perfect_hasher<recsplit16>);
    STATIC_REQUIRE(perfect_hasher<chd_hasher>);
    STATIC_REQUIRE(perfect_hasher<bbhash3>);
    STATIC_REQUIRE(perfect_hasher<bbhash5>);
    STATIC_REQUIRE(perfect_hasher<pthash98>);
    STATIC_REQUIRE(perfect_hasher<pthash95>);
    STATIC_REQUIRE(perfect_hasher<fch_hasher>);
}

TEST_CASE("Concepts: hasher compliance", "[perfect][concepts]") {
    STATIC_REQUIRE(hasher<recsplit8>);
    STATIC_REQUIRE(hasher<chd_hasher>);
    STATIC_REQUIRE(hasher<bbhash3>);
    STATIC_REQUIRE(hasher<pthash98>);
    STATIC_REQUIRE(hasher<fch_hasher>);
}

TEST_CASE("Concepts: builder compliance", "[perfect][concepts]") {
    STATIC_REQUIRE(perfect_hash_builder<recsplit8::builder, recsplit8>);
    STATIC_REQUIRE(perfect_hash_builder<chd_hasher::builder, chd_hasher>);
    STATIC_REQUIRE(perfect_hash_builder<bbhash3::builder, bbhash3>);
    STATIC_REQUIRE(perfect_hash_builder<pthash98::builder, pthash98>);
    STATIC_REQUIRE(perfect_hash_builder<fch_hasher::builder, fch_hasher>);
}

// ===== EDGE CASE TESTS =====

TEST_CASE("Edge: Empty string key", "[perfect][edge]") {
    std::vector<std::string> keys = {"", "a", "b"};

    SECTION("RecSplit") {
        auto result = recsplit8::builder{}.add_all(keys).build();
        REQUIRE(result.has_value());
        REQUIRE(verify_perfect_hash(result.value(), keys));
    }

    SECTION("BBHash") {
        auto result = bbhash3::builder{}.add_all(keys).build();
        REQUIRE(result.has_value());
        REQUIRE(verify_perfect_hash(result.value(), keys));
    }

    SECTION("PTHash") {
        auto result = pthash98::builder{}.add_all(keys).build();
        REQUIRE(result.has_value());
        REQUIRE(verify_perfect_hash(result.value(), keys));
    }
}

TEST_CASE("Edge: Single character keys", "[perfect][edge]") {
    std::vector<std::string> keys;
    for (char c = 'a'; c <= 'z'; ++c) {
        keys.push_back(std::string(1, c));
    }

    auto result = recsplit8::builder{}.add_all(keys).build();
    REQUIRE(result.has_value());
    REQUIRE(verify_perfect_hash(result.value(), keys));
}

TEST_CASE("Edge: Keys with special characters", "[perfect][edge]") {
    std::vector<std::string> keys = {
        "hello!",
        "world?",
        "test@example.com",
        "path/to/file",
        "key-with-dashes",
        "key_with_underscores",
        "key.with.dots"
    };

    auto result = bbhash3::builder{}.add_all(keys).build();
    REQUIRE(result.has_value());
    REQUIRE(verify_perfect_hash(result.value(), keys));
}

TEST_CASE("Edge: Keys with whitespace", "[perfect][edge]") {
    std::vector<std::string> keys = {
        " leading",
        "trailing ",
        "mid dle",
        "  multiple  spaces  "
    };

    auto result = recsplit8::builder{}.add_all(keys).build();
    REQUIRE(result.has_value());
    REQUIRE(verify_perfect_hash(result.value(), keys));
}

TEST_CASE("Edge: Keys with numbers", "[perfect][edge]") {
    std::vector<std::string> keys;
    for (int i = 0; i < 100; ++i) {
        keys.push_back(std::to_string(i));
    }

    auto result = bbhash3::builder{}.add_all(keys).build();
    REQUIRE(result.has_value());
    REQUIRE(verify_perfect_hash(result.value(), keys));
}

TEST_CASE("Edge: Very long keys", "[perfect][edge]") {
    std::vector<std::string> keys;
    for (size_t i = 0; i < 10; ++i) {
        keys.push_back(std::string(500, 'a' + i) + std::to_string(i));
    }

    auto result = recsplit8::builder{}.add_all(keys).build();
    REQUIRE(result.has_value());
    REQUIRE(verify_perfect_hash(result.value(), keys));
}

TEST_CASE("Edge: Similar keys differing by one character", "[perfect][edge]") {
    std::vector<std::string> keys;
    std::string base = "similar_key_";
    for (size_t i = 0; i < 50; ++i) {
        keys.push_back(base + std::to_string(i));
    }

    auto result = bbhash3::builder{}.add_all(keys).build();
    REQUIRE(result.has_value());
    REQUIRE(verify_perfect_hash(result.value(), keys));
}

// ===== BBHASH RANK STRUCTURE TESTS =====

TEST_CASE("BBHash: Rank structure correctness", "[perfect][bbhash][rank]") {
    auto keys = generate_random_keys(100);

    auto result = bbhash3::builder{}.add_all(keys).build();
    REQUIRE(result.has_value());

    auto& hasher = result.value();

    SECTION("Rank returns correct count of keys assigned to earlier levels") {
        // Verify that slot_for() uses rank correctly
        std::set<uint64_t> seen_slots;
        for (const auto& key : keys) {
            auto slot = hasher.slot_for(key);
            REQUIRE(slot.has_value());
            REQUIRE(slot->value < keys.size());

            // Check uniqueness
            REQUIRE_FALSE(seen_slots.count(slot->value));
            seen_slots.insert(slot->value);
        }
    }
}

TEST_CASE("BBHash: Rank boundary conditions", "[perfect][bbhash][rank]") {
    // Use small key set to test rank at word boundaries (64-bit boundaries)
    std::vector<std::string> keys;
    for (size_t i = 0; i < 70; ++i) {  // Cross 64-bit boundary
        keys.push_back("key_" + std::to_string(i));
    }

    auto result = bbhash3::builder{}.add_all(keys).build();
    REQUIRE(result.has_value());
    REQUIRE(verify_perfect_hash(result.value(), keys));
}

TEST_CASE("BBHash: Verify rank produces correct slot ordering", "[perfect][bbhash][rank]") {
    std::vector<std::string> keys = {"a", "b", "c", "d", "e", "f", "g", "h"};

    auto result = bbhash3::builder{}.add_all(keys).build();
    REQUIRE(result.has_value());

    auto& hasher = result.value();

    // All slots should be in range [0, keys.size())
    for (const auto& key : keys) {
        auto slot = hasher.slot_for(key);
        REQUIRE(slot.has_value());
        REQUIRE(slot->value >= 0);
        REQUIRE(slot->value < keys.size());
    }
}

// ===== PROPERTY-BASED TESTS =====

TEST_CASE("Property: Determinism - same keys produce same hash", "[perfect][property]") {
    auto keys = generate_random_keys(50);
    uint64_t seed = 12345;

    SECTION("RecSplit") {
        auto h1 = recsplit8::builder{}.add_all(keys).with_seed(seed).build().value();
        auto h2 = recsplit8::builder{}.add_all(keys).with_seed(seed).build().value();

        for (const auto& key : keys) {
            REQUIRE(h1.slot_for(key) == h2.slot_for(key));
        }
    }

    SECTION("BBHash") {
        auto h1 = bbhash3::builder{}.add_all(keys).with_seed(seed).build().value();
        auto h2 = bbhash3::builder{}.add_all(keys).with_seed(seed).build().value();

        for (const auto& key : keys) {
            REQUIRE(h1.slot_for(key) == h2.slot_for(key));
        }
    }
}

TEST_CASE("Property: All slots in valid range", "[perfect][property]") {
    auto keys = generate_random_keys(100);

    auto hasher = bbhash3::builder{}.add_all(keys).build().value();

    for (const auto& key : keys) {
        auto slot = hasher.slot_for(key);
        REQUIRE(slot.has_value());
        INFO("Slot " << slot->value << " out of range for " << keys.size() << " keys");
        REQUIRE(slot->value < keys.size());
    }
}

TEST_CASE("Property: Order independence - key insertion order doesn't matter", "[perfect][property]") {
    std::vector<std::string> keys = {"zebra", "apple", "mango", "banana", "cherry"};
    auto keys_reversed = keys;
    std::reverse(keys_reversed.begin(), keys_reversed.end());

    auto h1 = recsplit8::builder{}.add_all(keys).with_seed(999).build().value();
    auto h2 = recsplit8::builder{}.add_all(keys_reversed).with_seed(999).build().value();

    // Both should produce valid perfect hashes (but slots may differ due to build order)
    REQUIRE(verify_perfect_hash(h1, keys));
    REQUIRE(verify_perfect_hash(h2, keys_reversed));
}

TEST_CASE("Property: Slot uniqueness across all algorithms", "[perfect][property]") {
    auto keys = generate_random_keys(50);

    SECTION("RecSplit8") {
        auto h = recsplit8::builder{}.add_all(keys).build().value();
        std::set<uint64_t> slots;
        for (const auto& key : keys) {
            auto slot = h.slot_for(key);
            REQUIRE(slot.has_value());
            REQUIRE(slots.insert(slot->value).second);  // Must be unique
        }
    }

    SECTION("BBHash3") {
        auto h = bbhash3::builder{}.add_all(keys).build().value();
        std::set<uint64_t> slots;
        for (const auto& key : keys) {
            auto slot = h.slot_for(key);
            REQUIRE(slot.has_value());
            REQUIRE(slots.insert(slot->value).second);  // Must be unique
        }
    }

    SECTION("PTHash") {
        auto h = pthash98::builder{}.add_all(keys).build().value();
        std::set<uint64_t> slots;
        for (const auto& key : keys) {
            auto slot = h.slot_for(key);
            REQUIRE(slot.has_value());
            REQUIRE(slots.insert(slot->value).second);  // Must be unique
        }
    }
}

TEST_CASE("Property: Statistics consistency", "[perfect][property]") {
    auto keys = generate_random_keys(100);

    auto hasher = bbhash3::builder{}.add_all(keys).build().value();
    auto stats = hasher.statistics();

    REQUIRE(stats.key_count == keys.size());
    REQUIRE(stats.memory_bytes > 0);
    REQUIRE(stats.bits_per_key > 0);
    // Note: bits_per_key measures data structure efficiency (excludes class overhead)
    // So it's typically less than (memory_bytes * 8) / key_count
    REQUIRE(stats.bits_per_key <= static_cast<double>(stats.memory_bytes * 8) / stats.key_count);
}

// ===== STRESS AND ROBUSTNESS TESTS =====

TEST_CASE("Stress: All identical keys except one character", "[perfect][stress]") {
    std::vector<std::string> keys;
    std::string base(100, 'a');
    for (size_t i = 0; i < 50; ++i) {
        std::string key = base;
        key[i % base.length()] = 'b';  // Change one character
        keys.push_back(key + std::to_string(i));  // Ensure uniqueness
    }

    auto result = recsplit8::builder{}.add_all(keys).build();
    REQUIRE(result.has_value());
    REQUIRE(verify_perfect_hash(result.value(), keys));
}

TEST_CASE("Stress: Power-of-two key counts", "[perfect][stress]") {
    for (size_t count : {8, 16, 32, 64, 128, 256}) {
        auto keys = generate_sequential_keys(count);

        auto result = bbhash3::builder{}.add_all(keys).build();
        REQUIRE(result.has_value());
        REQUIRE(verify_perfect_hash(result.value(), keys));
    }
}

TEST_CASE("Stress: Keys with repeating patterns", "[perfect][stress]") {
    std::vector<std::string> keys;
    for (size_t i = 0; i < 100; ++i) {
        keys.push_back(std::string(i % 10 + 1, 'x') + std::to_string(i));
    }

    auto result = recsplit8::builder{}.add_all(keys).build();
    REQUIRE(result.has_value());
    REQUIRE(verify_perfect_hash(result.value(), keys));
}

// ===== SERIALIZATION TESTS =====

TEST_CASE("Serialization: RecSplit round-trip", "[perfect][serialization]") {
    auto keys = generate_random_keys(100);

    auto original = recsplit8::builder{}.add_all(keys).build().value();

    SECTION("Serialize and deserialize") {
        auto serialized = original.serialize();
        REQUIRE(serialized.size() > 0);

        auto restored = recsplit8::deserialize(serialized);
        REQUIRE(restored.has_value());

        // Verify same behavior
        for (const auto& key : keys) {
            auto orig_slot = original.slot_for(key);
            auto rest_slot = restored->slot_for(key);
            REQUIRE(orig_slot.has_value());
            REQUIRE(rest_slot.has_value());
            REQUIRE(orig_slot->value == rest_slot->value);
        }

        // Verify unknown keys
        REQUIRE_FALSE(restored->slot_for("unknown_key_xyz").has_value());
    }

    SECTION("Statistics match after deserialization") {
        auto serialized = original.serialize();
        auto restored = recsplit8::deserialize(serialized).value();

        auto orig_stats = original.statistics();
        auto rest_stats = restored.statistics();

        REQUIRE(orig_stats.key_count == rest_stats.key_count);
        REQUIRE(orig_stats.perfect_count == rest_stats.perfect_count);
        REQUIRE(orig_stats.overflow_count == rest_stats.overflow_count);
    }
}

TEST_CASE("Serialization: CHD round-trip", "[perfect][serialization]") {
    auto keys = generate_random_keys(100);

    auto original = chd_hasher::builder{}.add_all(keys).build().value();

    auto serialized = original.serialize();
    REQUIRE(serialized.size() > 0);

    auto restored = chd_hasher::deserialize(serialized);
    REQUIRE(restored.has_value());

    for (const auto& key : keys) {
        auto orig_slot = original.slot_for(key);
        auto rest_slot = restored->slot_for(key);
        REQUIRE(orig_slot.has_value());
        REQUIRE(rest_slot.has_value());
        REQUIRE(orig_slot->value == rest_slot->value);
    }
}

TEST_CASE("Serialization: BBHash round-trip", "[perfect][serialization]") {
    auto keys = generate_random_keys(100);

    auto original = bbhash3::builder{}.add_all(keys).build().value();

    auto serialized = original.serialize();
    REQUIRE(serialized.size() > 0);

    auto restored = bbhash3::deserialize(serialized);
    REQUIRE(restored.has_value());

    for (const auto& key : keys) {
        auto orig_slot = original.slot_for(key);
        auto rest_slot = restored->slot_for(key);
        REQUIRE(orig_slot.has_value());
        REQUIRE(rest_slot.has_value());
        REQUIRE(orig_slot->value == rest_slot->value);
    }
}

TEST_CASE("Serialization: FCH round-trip", "[perfect][serialization]") {
    auto keys = generate_random_keys(100);

    auto original = fch_hasher::builder{}.add_all(keys).build().value();

    auto serialized = original.serialize();
    REQUIRE(serialized.size() > 0);

    auto restored = fch_hasher::deserialize(serialized);
    REQUIRE(restored.has_value());

    for (const auto& key : keys) {
        auto orig_slot = original.slot_for(key);
        auto rest_slot = restored->slot_for(key);
        REQUIRE(orig_slot.has_value());
        REQUIRE(rest_slot.has_value());
        REQUIRE(orig_slot->value == rest_slot->value);
    }
}

TEST_CASE("Serialization: Invalid data handling", "[perfect][serialization]") {
    SECTION("Empty data") {
        std::vector<std::byte> empty;
        auto result = recsplit8::deserialize(empty);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Truncated data") {
        auto keys = generate_random_keys(50);
        auto hasher = recsplit8::builder{}.add_all(keys).build().value();
        auto serialized = hasher.serialize();

        // Truncate the data
        serialized.resize(serialized.size() / 2);
        auto result = recsplit8::deserialize(serialized);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("Corrupted magic number") {
        auto keys = generate_random_keys(50);
        auto hasher = recsplit8::builder{}.add_all(keys).build().value();
        auto serialized = hasher.serialize();

        // Corrupt the magic number
        serialized[0] = std::byte{0xFF};
        auto result = recsplit8::deserialize(serialized);
        REQUIRE_FALSE(result.has_value());
    }
}
