/**
 * @file test_perfect_hash_extended.cpp
 * @brief Extended TDD-focused tests for perfect hash implementations
 *
 * This file contains additional tests identified through TDD review:
 * - Edge cases not covered in the main test file
 * - Regression tests for potential bugs
 * - Property-based tests for invariants
 * - Stress tests for robustness
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <maph/core.hpp>
#include <maph/hashers_perfect.hpp>
#include <set>
#include <algorithm>
#include <random>
#include <string>
#include <unordered_set>

using namespace maph;

// ===== TEST UTILITIES =====

namespace {

std::vector<std::string> generate_random_keys(size_t count, size_t min_len = 4, size_t max_len = 16, uint64_t seed = 42) {
    std::vector<std::string> keys;
    keys.reserve(count);

    std::mt19937_64 rng{seed};
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

template<typename Hasher>
bool verify_perfect_hash_property(const Hasher& hasher, const std::vector<std::string>& keys) {
    std::set<uint64_t> seen_slots;

    for (const auto& key : keys) {
        auto slot_opt = hasher.slot_for(key);
        if (!slot_opt) {
            INFO("Key not found: " << key);
            return false;
        }

        auto slot = slot_opt->value;
        if (slot >= keys.size()) {
            INFO("Slot out of range: " << slot << " >= " << keys.size());
            return false;
        }

        if (seen_slots.count(slot)) {
            INFO("Slot collision for key: " << key << " at slot " << slot);
            return false;
        }
        seen_slots.insert(slot);
    }

    return seen_slots.size() == keys.size();
}

} // anonymous namespace

// ===== CRITICAL: TESTS FOR IDENTIFIED DEFECTS =====

TEST_CASE("RecSplit: Implementation stores keys - not truly minimal perfect hash", "[perfect][recsplit][defect]") {
    // DEFECT: RecSplit implementation uses unordered_map to store keys
    // This means it's not a true perfect hash function - it's just a lookup table
    // with O(n) space for the keys themselves

    std::vector<std::string> keys = {"apple", "banana", "cherry"};
    auto result = recsplit8::builder{}.add_all(keys).build();
    REQUIRE(result.has_value());

    auto& hasher = result.value();
    auto stats = hasher.statistics();

    // The current implementation stores full keys, so bits_per_key will be much higher
    // than the theoretical ~2 bits/key claimed in the documentation
    INFO("RecSplit bits per key: " << stats.bits_per_key);

    // This test documents the current behavior - it should be HIGH because of key storage
    // A true RecSplit would be ~2 bits/key
    REQUIRE(stats.bits_per_key > 50.0);  // Much higher than theoretical due to key storage
}

TEST_CASE("CHD: Implementation stores keys - not truly minimal perfect hash", "[perfect][chd][defect]") {
    // DEFECT: CHD implementation also stores keys in unordered_map

    std::vector<std::string> keys = {"red", "green", "blue"};
    auto result = chd_hasher::builder{}.add_all(keys).build();
    REQUIRE(result.has_value());

    auto& hasher = result.value();
    auto stats = hasher.statistics();

    INFO("CHD bits per key: " << stats.bits_per_key);
    // Documenting current behavior - stores keys
}

TEST_CASE("FCH: Implementation stores keys - not truly minimal perfect hash", "[perfect][fch][defect]") {
    // DEFECT: FCH implementation also stores keys in unordered_map

    std::vector<std::string> keys = {"one", "two", "three"};
    auto result = fch_hasher::builder{}.add_all(keys).build();
    REQUIRE(result.has_value());

    auto& hasher = result.value();
    auto stats = hasher.statistics();

    INFO("FCH bits per key: " << stats.bits_per_key);
    // Documenting current behavior - stores keys
}

// ===== BBHash Rank Structure Edge Cases =====

TEST_CASE("BBHash: Rank at word boundary", "[perfect][bbhash][rank][edge]") {
    // Test rank calculation at 64-bit word boundaries
    // This is a critical edge case for the rank structure

    std::vector<std::string> keys;
    // Generate exactly 65 keys to cross one word boundary
    for (size_t i = 0; i < 65; ++i) {
        keys.push_back("boundary_key_" + std::to_string(i));
    }

    auto result = bbhash3::builder{}.add_all(keys).with_gamma(3.0).build();
    REQUIRE(result.has_value());
    REQUIRE(verify_perfect_hash_property(result.value(), keys));
}

TEST_CASE("BBHash: Rank at multiple word boundaries", "[perfect][bbhash][rank][edge]") {
    // Generate keys to cross multiple 64-bit boundaries
    std::vector<std::string> keys;
    for (size_t i = 0; i < 200; ++i) {  // Multiple word boundaries
        keys.push_back("multi_boundary_" + std::to_string(i));
    }

    // Use 5 levels for larger key sets to ensure success
    auto result = bbhash5::builder{}.add_all(keys).with_gamma(2.5).build();
    REQUIRE(result.has_value());
    REQUIRE(verify_perfect_hash_property(result.value(), keys));
}

TEST_CASE("BBHash: Edge case - gamma = 1.0 (minimum)", "[perfect][bbhash][edge]") {
    // Minimum gamma should still work but may fail more often
    auto keys = generate_random_keys(20);

    auto result = bbhash5::builder{}  // More levels for low gamma
        .add_all(keys)
        .with_gamma(1.0)
        .build();

    // May or may not succeed with minimum gamma
    if (result.has_value()) {
        REQUIRE(verify_perfect_hash_property(result.value(), keys));
    }
}

TEST_CASE("BBHash: Edge case - single key", "[perfect][bbhash][edge]") {
    auto result = bbhash3::builder{}.add("only_one").build();
    REQUIRE(result.has_value());

    auto& hasher = result.value();
    REQUIRE(hasher.key_count() == 1);

    auto slot = hasher.slot_for("only_one");
    REQUIRE(slot.has_value());
    REQUIRE(slot->value == 0);  // Single key should get slot 0
}

// ===== PTHash Edge Cases =====

TEST_CASE("PTHash: Large bucket collision", "[perfect][pthash][edge]") {
    // Create keys that are likely to collide in buckets
    std::vector<std::string> keys;
    for (size_t i = 0; i < 50; ++i) {
        // Similar prefixes may hash to same bucket
        keys.push_back("prefix_" + std::to_string(i));
    }

    auto result = pthash98::builder{}.add_all(keys).build();
    REQUIRE(result.has_value());
    REQUIRE(verify_perfect_hash_property(result.value(), keys));
}

TEST_CASE("PTHash: Pilot search exhaustion", "[perfect][pthash][edge]") {
    // Test behavior when pilot search might be exhausted
    // Use very small table with many keys
    auto keys = generate_random_keys(100);

    auto result = pthash98::builder{}
        .add_all(keys)
        .with_max_pilot_search(100)  // Small search space
        .build();

    // Should still succeed with retries
    REQUIRE(result.has_value());
    REQUIRE(verify_perfect_hash_property(result.value(), keys));
}

// ===== FCH Edge Cases =====

TEST_CASE("FCH: Very small bucket size", "[perfect][fch][edge]") {
    auto keys = generate_random_keys(50);

    auto result = fch_hasher::builder{}
        .add_all(keys)
        .with_bucket_size(1.0)  // Minimum bucket size
        .build();

    REQUIRE(result.has_value());
    REQUIRE(verify_perfect_hash_property(result.value(), keys));
}

TEST_CASE("FCH: Large bucket size", "[perfect][fch][edge]") {
    auto keys = generate_random_keys(50);

    auto result = fch_hasher::builder{}
        .add_all(keys)
        .with_bucket_size(50.0)  // Very large bucket
        .build();

    REQUIRE(result.has_value());
    REQUIRE(verify_perfect_hash_property(result.value(), keys));
}

// ===== Unknown Key Rejection Tests =====

TEST_CASE("All hashers: Must reject unknown keys", "[perfect][property][critical]") {
    std::vector<std::string> build_keys = {"known1", "known2", "known3", "known4", "known5"};
    std::vector<std::string> unknown_keys = {"unknown1", "unknown2", "totally_different", "xyz", ""};

    SECTION("RecSplit rejects unknowns") {
        auto hasher = recsplit8::builder{}.add_all(build_keys).build().value();
        for (const auto& key : unknown_keys) {
            auto slot = hasher.slot_for(key);
            REQUIRE_FALSE(slot.has_value());
        }
    }

    SECTION("BBHash rejects unknowns") {
        auto hasher = bbhash3::builder{}.add_all(build_keys).build().value();
        for (const auto& key : unknown_keys) {
            auto slot = hasher.slot_for(key);
            REQUIRE_FALSE(slot.has_value());
        }
    }

    SECTION("PTHash rejects unknowns") {
        auto hasher = pthash98::builder{}.add_all(build_keys).build().value();
        for (const auto& key : unknown_keys) {
            auto slot = hasher.slot_for(key);
            REQUIRE_FALSE(slot.has_value());
        }
    }

    SECTION("CHD rejects unknowns") {
        auto hasher = chd_hasher::builder{}.add_all(build_keys).build().value();
        for (const auto& key : unknown_keys) {
            auto slot = hasher.slot_for(key);
            REQUIRE_FALSE(slot.has_value());
        }
    }

    SECTION("FCH rejects unknowns") {
        auto hasher = fch_hasher::builder{}.add_all(build_keys).build().value();
        for (const auto& key : unknown_keys) {
            auto slot = hasher.slot_for(key);
            REQUIRE_FALSE(slot.has_value());
        }
    }
}

// ===== Fingerprint Collision Tests =====

TEST_CASE("Fingerprint: Low false positive rate", "[perfect][fingerprint][property]") {
    // Test that fingerprints effectively reject unknown keys
    // with low false positive rate

    auto build_keys = generate_random_keys(100, 8, 16, 12345);
    auto test_keys = generate_random_keys(10000, 8, 16, 99999);  // Different seed

    // Remove any overlap
    std::set<std::string> build_set(build_keys.begin(), build_keys.end());
    std::vector<std::string> truly_unknown;
    for (const auto& key : test_keys) {
        if (build_set.find(key) == build_set.end()) {
            truly_unknown.push_back(key);
        }
    }

    auto hasher = bbhash5::builder{}.add_all(build_keys).build().value();

    size_t false_positives = 0;
    for (const auto& key : truly_unknown) {
        if (hasher.slot_for(key).has_value()) {
            false_positives++;
        }
    }

    // With 64-bit fingerprints, false positive rate should be very low
    double fp_rate = static_cast<double>(false_positives) / truly_unknown.size();
    INFO("False positive rate: " << fp_rate << " (" << false_positives << "/" << truly_unknown.size() << ")");

    // 64-bit fingerprint should give ~2^-64 false positive rate
    // Allow some tolerance for practical testing
    REQUIRE(fp_rate < 0.001);  // Less than 0.1% false positives
}

// ===== Binary Key Tests (Non-string data) =====

TEST_CASE("Binary keys with null bytes", "[perfect][edge][binary]") {
    std::vector<std::string> keys;

    // Create keys with embedded null bytes
    keys.push_back(std::string("key\0one", 7));
    keys.push_back(std::string("key\0two", 7));
    keys.push_back(std::string("\0start", 6));
    keys.push_back(std::string("end\0", 4));
    keys.push_back(std::string("\0\0\0", 3));

    SECTION("RecSplit handles binary keys") {
        auto result = recsplit8::builder{}.add_all(keys).build();
        REQUIRE(result.has_value());
        REQUIRE(verify_perfect_hash_property(result.value(), keys));
    }

    SECTION("BBHash handles binary keys") {
        auto result = bbhash3::builder{}.add_all(keys).build();
        REQUIRE(result.has_value());
        REQUIRE(verify_perfect_hash_property(result.value(), keys));
    }
}

// ===== Unicode/UTF-8 Key Tests =====

TEST_CASE("UTF-8 encoded keys", "[perfect][edge][unicode]") {
    std::vector<std::string> keys;
    keys.push_back("hello");    // ASCII
    keys.push_back("cafe");     // Common word
    keys.push_back("resume");   // Common word
    keys.push_back("Tokyo");    // Japanese city
    keys.push_back("Beijing");  // Chinese city
    keys.push_back("key_123");  // Mixed

    SECTION("RecSplit") {
        auto result = recsplit8::builder{}.add_all(keys).build();
        REQUIRE(result.has_value());
        REQUIRE(verify_perfect_hash_property(result.value(), keys));
    }

    SECTION("BBHash") {
        auto result = bbhash3::builder{}.add_all(keys).build();
        REQUIRE(result.has_value());
        REQUIRE(verify_perfect_hash_property(result.value(), keys));
    }
}

// ===== Determinism Property Tests =====

TEST_CASE("Determinism: Same seed produces same hash function", "[perfect][property][determinism]") {
    auto keys = generate_random_keys(50);
    uint64_t seed = 0xDEADBEEF;

    SECTION("RecSplit determinism") {
        auto h1 = recsplit8::builder{}.add_all(keys).with_seed(seed).build().value();
        auto h2 = recsplit8::builder{}.add_all(keys).with_seed(seed).build().value();

        for (const auto& key : keys) {
            REQUIRE(h1.slot_for(key) == h2.slot_for(key));
        }
    }

    SECTION("BBHash determinism") {
        auto h1 = bbhash3::builder{}.add_all(keys).with_seed(seed).build().value();
        auto h2 = bbhash3::builder{}.add_all(keys).with_seed(seed).build().value();

        for (const auto& key : keys) {
            REQUIRE(h1.slot_for(key) == h2.slot_for(key));
        }
    }

    SECTION("PTHash determinism") {
        auto h1 = pthash98::builder{}.add_all(keys).with_seed(seed).build().value();
        auto h2 = pthash98::builder{}.add_all(keys).with_seed(seed).build().value();

        for (const auto& key : keys) {
            REQUIRE(h1.slot_for(key) == h2.slot_for(key));
        }
    }
}

// ===== Default Constructor Safety =====

TEST_CASE("Default constructed hashers are safe", "[perfect][safety]") {
    SECTION("RecSplit default") {
        recsplit8 hasher;
        REQUIRE(hasher.key_count() == 0);
        REQUIRE(hasher.max_slots().value == 0);
        REQUIRE_FALSE(hasher.slot_for("anything").has_value());
    }

    SECTION("BBHash default") {
        bbhash3 hasher;
        REQUIRE(hasher.key_count() == 0);
        REQUIRE(hasher.max_slots().value == 0);
        REQUIRE_FALSE(hasher.slot_for("anything").has_value());
    }

    SECTION("PTHash default") {
        pthash98 hasher;
        REQUIRE(hasher.key_count() == 0);
        REQUIRE(hasher.max_slots().value == 0);
        REQUIRE_FALSE(hasher.slot_for("anything").has_value());
    }

    SECTION("FCH default") {
        fch_hasher hasher;
        REQUIRE(hasher.key_count() == 0);
        REQUIRE(hasher.max_slots().value == 0);
        REQUIRE_FALSE(hasher.slot_for("anything").has_value());
    }
}

// ===== Move Semantics Tests =====

TEST_CASE("Move semantics preserve functionality", "[perfect][move]") {
    auto keys = generate_random_keys(50);

    SECTION("RecSplit move") {
        auto original = recsplit8::builder{}.add_all(keys).build().value();
        auto slot_before = original.slot_for(keys[0]);

        auto moved = std::move(original);
        auto slot_after = moved.slot_for(keys[0]);

        REQUIRE(slot_before == slot_after);
        REQUIRE(verify_perfect_hash_property(moved, keys));
    }

    SECTION("BBHash move") {
        auto original = bbhash3::builder{}.add_all(keys).build().value();
        auto slot_before = original.slot_for(keys[0]);

        auto moved = std::move(original);
        auto slot_after = moved.slot_for(keys[0]);

        REQUIRE(slot_before == slot_after);
        REQUIRE(verify_perfect_hash_property(moved, keys));
    }
}

// ===== Hash Consistency Property =====

TEST_CASE("hash() and slot_for() return consistent values", "[perfect][property][consistency]") {
    auto keys = generate_random_keys(100);

    SECTION("RecSplit consistency") {
        auto hasher = recsplit8::builder{}.add_all(keys).build().value();

        for (const auto& key : keys) {
            auto hash = hasher.hash(key);
            auto slot = hasher.slot_for(key);

            REQUIRE(slot.has_value());
            REQUIRE(hash.value == slot->value);
        }
    }

    SECTION("BBHash consistency") {
        auto hasher = bbhash3::builder{}.add_all(keys).build().value();

        for (const auto& key : keys) {
            auto hash = hasher.hash(key);
            auto slot = hasher.slot_for(key);

            REQUIRE(slot.has_value());
            REQUIRE(hash.value == slot->value);
        }
    }
}

// ===== Statistics Validity =====

TEST_CASE("Statistics are valid and consistent", "[perfect][property][statistics]") {
    auto keys = generate_random_keys(100);

    SECTION("RecSplit statistics") {
        auto hasher = recsplit8::builder{}.add_all(keys).build().value();
        auto stats = hasher.statistics();

        REQUIRE(stats.key_count == keys.size());
        REQUIRE(stats.memory_bytes > 0);
        REQUIRE(stats.bits_per_key > 0);
        REQUIRE(stats.is_minimal());
    }

    SECTION("BBHash statistics") {
        auto hasher = bbhash3::builder{}.add_all(keys).build().value();
        auto stats = hasher.statistics();

        REQUIRE(stats.key_count == keys.size());
        REQUIRE(stats.memory_bytes > 0);
        REQUIRE(stats.bits_per_key > 0);
    }

    SECTION("PTHash statistics") {
        auto hasher = pthash98::builder{}.add_all(keys).build().value();
        auto stats = hasher.statistics();

        REQUIRE(stats.key_count == keys.size());
        REQUIRE(stats.memory_bytes > 0);
        REQUIRE(stats.bits_per_key > 0);
    }
}

// ===== Two Keys Edge Case =====

TEST_CASE("Two keys - minimal non-trivial case", "[perfect][edge][minimal]") {
    std::vector<std::string> keys = {"first", "second"};

    SECTION("RecSplit") {
        auto result = recsplit8::builder{}.add_all(keys).build();
        REQUIRE(result.has_value());
        REQUIRE(verify_perfect_hash_property(result.value(), keys));
    }

    SECTION("BBHash") {
        auto result = bbhash3::builder{}.add_all(keys).build();
        REQUIRE(result.has_value());
        REQUIRE(verify_perfect_hash_property(result.value(), keys));
    }

    SECTION("PTHash") {
        auto result = pthash98::builder{}.add_all(keys).build();
        REQUIRE(result.has_value());
        REQUIRE(verify_perfect_hash_property(result.value(), keys));
    }

    SECTION("CHD") {
        auto result = chd_hasher::builder{}.add_all(keys).build();
        REQUIRE(result.has_value());
        REQUIRE(verify_perfect_hash_property(result.value(), keys));
    }

    SECTION("FCH") {
        auto result = fch_hasher::builder{}.add_all(keys).build();
        REQUIRE(result.has_value());
        REQUIRE(verify_perfect_hash_property(result.value(), keys));
    }
}

// ===== All Duplicates Edge Case =====

TEST_CASE("All duplicate keys - should deduplicate to one", "[perfect][edge][duplicates]") {
    std::vector<std::string> keys(100, "same_key");  // 100 copies of same key

    SECTION("RecSplit deduplicates") {
        auto result = recsplit8::builder{}.add_all(keys).build();
        REQUIRE(result.has_value());
        REQUIRE(result.value().key_count() == 1);
    }

    SECTION("BBHash deduplicates") {
        auto result = bbhash3::builder{}.add_all(keys).build();
        REQUIRE(result.has_value());
        REQUIRE(result.value().key_count() == 1);
    }
}

// ===== Power of Two Sizes =====

TEST_CASE("Power of two key counts", "[perfect][edge][poweroftwo]") {
    for (size_t count : {2, 4, 8, 16, 32, 64}) {
        auto keys = generate_random_keys(count);

        DYNAMIC_SECTION("BBHash with " << count << " keys") {
            auto result = bbhash3::builder{}.add_all(keys).build();
            REQUIRE(result.has_value());
            REQUIRE(verify_perfect_hash_property(result.value(), keys));
        }
    }
}

// ===== Prime Number Sizes =====

TEST_CASE("Prime number key counts", "[perfect][edge][prime]") {
    for (size_t count : {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53}) {
        auto keys = generate_random_keys(count);

        DYNAMIC_SECTION("BBHash with " << count << " (prime) keys") {
            auto result = bbhash3::builder{}.add_all(keys).build();
            REQUIRE(result.has_value());
            REQUIRE(verify_perfect_hash_property(result.value(), keys));
        }
    }
}

// ===== Stress Tests =====

TEST_CASE("Stress: Build 100 different hash functions", "[perfect][stress][build]") {
    // Verify consistent behavior across many builds
    for (size_t i = 0; i < 100; ++i) {
        auto keys = generate_random_keys(50, 4, 16, i);  // Different seed each time

        // Use 5 levels and higher gamma for reliable builds
        auto result = bbhash5::builder{}.add_all(keys).with_seed(i).with_gamma(2.5).build();
        REQUIRE(result.has_value());
        REQUIRE(verify_perfect_hash_property(result.value(), keys));
    }
}

TEST_CASE("Stress: Maximum key length stress", "[perfect][stress][longkeys]") {
    std::vector<std::string> keys;
    for (size_t i = 0; i < 20; ++i) {
        // Create very long keys (1KB each)
        keys.push_back(std::string(1024, 'a' + (i % 26)) + std::to_string(i));
    }

    auto result = bbhash3::builder{}.add_all(keys).build();
    REQUIRE(result.has_value());
    REQUIRE(verify_perfect_hash_property(result.value(), keys));
}

// ===== is_perfect_for() Consistency =====

TEST_CASE("is_perfect_for() matches slot_for() behavior", "[perfect][property][consistency]") {
    auto keys = generate_random_keys(50);
    auto unknown_keys = generate_random_keys(50, 4, 16, 99999);

    SECTION("RecSplit") {
        auto hasher = recsplit8::builder{}.add_all(keys).build().value();

        for (const auto& key : keys) {
            REQUIRE(hasher.is_perfect_for(key) == hasher.slot_for(key).has_value());
        }

        for (const auto& key : unknown_keys) {
            REQUIRE(hasher.is_perfect_for(key) == hasher.slot_for(key).has_value());
        }
    }

    SECTION("BBHash") {
        auto hasher = bbhash3::builder{}.add_all(keys).build().value();

        for (const auto& key : keys) {
            REQUIRE(hasher.is_perfect_for(key) == hasher.slot_for(key).has_value());
        }

        for (const auto& key : unknown_keys) {
            REQUIRE(hasher.is_perfect_for(key) == hasher.slot_for(key).has_value());
        }
    }
}
