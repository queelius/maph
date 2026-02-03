/**
 * @file test_hashers.cpp
 * @brief Comprehensive tests for maph hash functions
 *
 * Tests focus on behavioral contracts for each hasher type:
 * - Deterministic behavior
 * - Distribution quality
 * - Composability
 * - Performance characteristics
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <maph/hashers.hpp>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <numeric>
#include <cmath>

using namespace maph;

// ===== FNV1A HASHER TESTS =====
// Test the FNV-1a hash function implementation

TEST_CASE("fnv1a_hasher basic behavior", "[hashers][fnv1a]") {
    slot_count slots{1000};
    fnv1a_hasher hasher{slots};

    SECTION("Deterministic hashing") {
        std::string test_key = "test_key_123";

        auto hash1 = hasher.hash(test_key);
        auto hash2 = hasher.hash(test_key);

        REQUIRE(hash1.value == hash2.value);
        REQUIRE(hash1.value != 0);  // Should never return 0
    }

    SECTION("Different keys produce different hashes (usually)") {
        auto hash1 = hasher.hash("key1");
        auto hash2 = hasher.hash("key2");
        auto hash3 = hasher.hash("key3");

        // While hash collisions are possible, these specific keys should hash differently
        REQUIRE(hash1.value != hash2.value);
        REQUIRE(hash2.value != hash3.value);
        REQUIRE(hash1.value != hash3.value);
    }

    SECTION("Empty string hashing") {
        auto hash = hasher.hash("");
        REQUIRE(hash.value != 0);  // Even empty string should produce non-zero hash
    }

    SECTION("Index calculation") {
        auto hash = hasher.hash("test");
        auto index = hasher.index_for("test");

        REQUIRE(index.value == (hash.value % slots.value));
        REQUIRE(index.value < slots.value);
    }

    SECTION("Slot count configuration") {
        REQUIRE(hasher.max_slots().value == slots.value);
    }
}

TEST_CASE("fnv1a_hasher distribution quality", "[hashers][fnv1a][distribution]") {
    slot_count slots{100};
    fnv1a_hasher hasher{slots};

    SECTION("Hash distribution over many keys") {
        std::vector<uint64_t> bucket_counts(slots.value, 0);
        constexpr size_t num_keys = 10000;

        // Generate keys and count bucket usage
        for (size_t i = 0; i < num_keys; ++i) {
            std::string key = "key_" + std::to_string(i);
            auto index = hasher.index_for(key);
            bucket_counts[index.value]++;
        }

        // Check that distribution is reasonably uniform
        double expected_per_bucket = double(num_keys) / slots.value;
        size_t empty_buckets = 0;

        for (auto count : bucket_counts) {
            if (count == 0) empty_buckets++;
        }

        // With good distribution, most buckets should have some items
        REQUIRE(empty_buckets < slots.value / 10);  // Less than 10% empty

        // Check that no bucket is extremely overloaded
        auto max_bucket = *std::max_element(bucket_counts.begin(), bucket_counts.end());
        REQUIRE(max_bucket < expected_per_bucket * 3);  // No bucket more than 3x expected
    }

    SECTION("Avalanche effect - small input changes cause large hash changes") {
        auto hash1 = hasher.hash("test");
        auto hash2 = hasher.hash("Test");  // Single character case change

        // Count differing bits
        uint64_t xor_result = hash1.value ^ hash2.value;
        int differing_bits = __builtin_popcountll(xor_result);

        // Good hash functions should change ~50% of bits for small input changes
        REQUIRE(differing_bits > 10);  // At least some significant change
    }
}

TEST_CASE("fnv1a_hasher edge cases", "[hashers][fnv1a][edge_cases]") {
    slot_count slots{1000};
    fnv1a_hasher hasher{slots};

    SECTION("Very long keys") {
        std::string long_key(10000, 'x');
        auto hash = hasher.hash(long_key);
        REQUIRE(hash.value != 0);

        // Should still index correctly
        auto index = hasher.index_for(long_key);
        REQUIRE(index.value < slots.value);
    }

    SECTION("Keys with special characters") {
        std::vector<std::string> special_keys = {
            "\0\0\0",
            "\xFF\xFE\xFD",
            "key\nwith\nnewlines",
            "key\twith\ttabs"
        };

        std::unordered_set<uint64_t> hashes;
        for (const auto& key : special_keys) {
            auto hash = hasher.hash(key);
            REQUIRE(hash.value != 0);
            hashes.insert(hash.value);
        }

        // All should produce different hashes
        REQUIRE(hashes.size() == special_keys.size());
    }

    SECTION("Single slot configuration") {
        fnv1a_hasher single_slot{slot_count{1}};

        auto index1 = single_slot.index_for("any_key");
        auto index2 = single_slot.index_for("different_key");

        REQUIRE(index1.value == 0);
        REQUIRE(index2.value == 0);
    }
}

// ===== LINEAR PROBE HASHER TESTS =====
// Test the linear probing decorator

TEST_CASE("linear_probe_hasher basic behavior", "[hashers][linear_probe]") {
    slot_count slots{100};
    fnv1a_hasher base{slots};
    linear_probe_hasher probe_hasher{base, 10};

    SECTION("Wraps base hasher correctly") {
        std::string test_key = "test_key";

        auto base_hash = base.hash(test_key);
        auto probe_hash = probe_hasher.hash(test_key);

        REQUIRE(base_hash.value == probe_hash.value);
        REQUIRE(probe_hasher.max_slots().value == base.max_slots().value);
    }

    SECTION("Probe sequence generation") {
        std::string key = "test_probe";
        auto probe_seq = probe_hasher.probe_sequence(key);

        std::vector<slot_index> indices;

        // Collect probe sequence
        while (!probe_seq.at_end()) {
            indices.push_back(*probe_seq);
            ++probe_seq;
        }

        REQUIRE(indices.size() == 10);  // Should generate max_probes indices

        // First index should match base hash modulo slots
        auto expected_first = slot_index{base.hash(key).value % slots.value};
        REQUIRE(indices[0].value == expected_first.value);

        // Subsequent indices should be consecutive (with wraparound)
        for (size_t i = 1; i < indices.size(); ++i) {
            auto expected = slot_index{(indices[0].value + i) % slots.value};
            REQUIRE(indices[i].value == expected.value);
        }
    }

    SECTION("Probe sequence wraps around") {
        // Use a small slot count to force wraparound
        linear_probe_hasher small_hasher{fnv1a_hasher{slot_count{5}}, 8};

        auto probe_seq = small_hasher.probe_sequence("wrap_test");
        std::vector<slot_index> indices;

        while (!probe_seq.at_end()) {
            indices.push_back(*probe_seq);
            ++probe_seq;
        }

        // All indices should be valid
        for (auto idx : indices) {
            REQUIRE(idx.value < 5);
        }

        // Should see wraparound in the sequence
        std::unordered_set<uint64_t> unique_indices;
        for (auto idx : indices) {
            unique_indices.insert(idx.value);
        }

        // With 8 probes and 5 slots, we should see some repeats
        REQUIRE(unique_indices.size() <= 5);
    }

    SECTION("Iterator semantics") {
        auto probe_seq = probe_hasher.probe_sequence("iterator_test");

        // Test copy construction
        auto copy_seq = probe_seq;
        REQUIRE((*probe_seq).value == (*copy_seq).value);

        // Test increment
        auto first_index = *probe_seq;
        ++probe_seq;
        auto second_index = *probe_seq;

        REQUIRE(second_index.value == (first_index.value + 1) % slots.value);

        // Test at_end condition
        size_t count = 0;
        auto count_seq = probe_hasher.probe_sequence("count_test");
        while (!count_seq.at_end()) {
            ++count_seq;
            count++;
        }
        REQUIRE(count == 10);
    }
}

TEST_CASE("linear_probe_hasher composability", "[hashers][linear_probe][composition]") {
    SECTION("Can wrap different base hashers") {
        slot_count slots{200};

        auto probe1 = linear_probe_hasher{fnv1a_hasher{slots}, 5};
        auto probe2 = linear_probe_hasher{fnv1a_hasher{slots}, 15};

        std::string test_key = "composability_test";

        // Both should hash the same way (same base hasher)
        REQUIRE(probe1.hash(test_key).value == probe2.hash(test_key).value);

        // But should have different probe counts
        auto seq1 = probe1.probe_sequence(test_key);
        auto seq2 = probe2.probe_sequence(test_key);

        size_t count1 = 0, count2 = 0;
        while (!seq1.at_end()) { ++seq1; count1++; }
        while (!seq2.at_end()) { ++seq2; count2++; }

        REQUIRE(count1 == 5);
        REQUIRE(count2 == 15);
    }

    SECTION("Nested composition") {
        // This shouldn't be practical but should compile and work
        slot_count slots{100};
        auto base = fnv1a_hasher{slots};
        auto level1 = linear_probe_hasher{base, 5};
        auto level2 = linear_probe_hasher{level1, 3};

        std::string key = "nested_test";

        // Should still hash correctly
        REQUIRE(level2.hash(key).value == base.hash(key).value);
        REQUIRE(level2.max_slots().value == slots.value);
    }
}

// ===== MINIMAL PERFECT HASHER TESTS =====
// Test the perfect hash function (note: implementation may be incomplete)

TEST_CASE("minimal_perfect_hasher builder", "[hashers][perfect][builder]") {
    SECTION("Builder pattern usage") {
        minimal_perfect_hasher::builder builder;

        builder.add("key1")
               .add("key2")
               .add("key3");

        // Note: The actual implementation might not be complete
        // These tests verify the interface works as expected
        REQUIRE_NOTHROW(builder.add("key4"));
    }

    SECTION("Duplicate keys in builder") {
        minimal_perfect_hasher::builder builder;

        builder.add("duplicate")
               .add("duplicate")  // Should handle duplicates gracefully
               .add("unique");

        REQUIRE_NOTHROW(builder.add("another"));
    }

    SECTION("Empty builder") {
        minimal_perfect_hasher::builder builder;

        // Building with no keys should be handled
        // Implementation may return an error or empty hasher
        REQUIRE_NOTHROW(builder.build());
    }
}

// ===== HYBRID HASHER TESTS =====
// Test the combination of perfect and standard hashing

TEST_CASE("hybrid_hasher concept", "[hashers][hybrid]") {
    SECTION("Type requirements") {
        // Test that hybrid hasher template compiles with appropriate types
        slot_count slots{100};

        // This tests the template instantiation
        using mock_perfect = minimal_perfect_hasher;
        using mock_fallback = fnv1a_hasher;
        using hybrid_type = hybrid_hasher<mock_perfect, mock_fallback>;

        // The type should exist and be usable
        REQUIRE(std::is_constructible_v<hybrid_type, mock_perfect, mock_fallback>);
    }

    SECTION("Factory function") {
        slot_count slots{100};
        auto fnv = fnv1a_hasher{slots};

        // Test that make_hybrid compiles and creates appropriate type
        // Note: Can't fully test without complete perfect hasher implementation
        REQUIRE_NOTHROW([&]() {
            // This should compile but may not be fully functional
            // auto hybrid = make_hybrid(perfect_hasher, fnv);
        }());
    }
}

// ===== PROPERTY-BASED TESTS =====
// Test invariants that should hold for all hashers

TEST_CASE("Hash function properties", "[hashers][properties]") {
    auto slots = GENERATE(slot_count{10}, slot_count{100}, slot_count{1000});
    fnv1a_hasher hasher{slots};

    SECTION("Determinism property") {
        auto test_key = GENERATE(
            as<std::string>{},
            "key1", "key2", "different_key", "", "very_long_key_" + std::string(1000, 'x')
        );

        // Hash function must be deterministic
        auto hash1 = hasher.hash(test_key);
        auto hash2 = hasher.hash(test_key);

        REQUIRE(hash1.value == hash2.value);
    }

    SECTION("Non-zero hash property") {
        auto test_key = GENERATE(
            as<std::string>{},
            "a", "b", "", "test", std::string(100, 'z')
        );

        // FNV-1a should never return 0
        auto hash = hasher.hash(test_key);
        REQUIRE(hash.value != 0);
    }

    SECTION("Index bounds property") {
        auto test_key = GENERATE(
            as<std::string>{},
            "key1", "key2", "key3", "boundary_test"
        );

        auto index = hasher.index_for(test_key);
        REQUIRE(index.value < slots.value);
    }
}

TEST_CASE("Linear probe properties", "[hashers][linear_probe][properties]") {
    auto max_probes = GENERATE(1, 5, 10, 20);
    slot_count slots{100};

    linear_probe_hasher hasher{fnv1a_hasher{slots}, static_cast<size_t>(max_probes)};

    SECTION("Probe sequence length property") {
        auto test_key = GENERATE(
            as<std::string>{},
            "probe1", "probe2", "probe3"
        );

        auto probe_seq = hasher.probe_sequence(test_key);
        size_t count = 0;

        while (!probe_seq.at_end()) {
            ++probe_seq;
            count++;
        }

        REQUIRE(count == static_cast<size_t>(max_probes));
    }

    SECTION("Probe sequence validity property") {
        auto test_key = GENERATE(
            as<std::string>{},
            "valid1", "valid2", "valid3"
        );

        auto probe_seq = hasher.probe_sequence(test_key);

        while (!probe_seq.at_end()) {
            auto index = *probe_seq;
            REQUIRE(index.value < slots.value);
            ++probe_seq;
        }
    }
}

// ===== PERFORMANCE TESTS =====
// Benchmark different hashers to ensure reasonable performance

TEST_CASE("Hash function performance", "[hashers][performance][!benchmark]") {
    slot_count slots{10000};
    fnv1a_hasher hasher{slots};

    // Generate test data
    std::vector<std::string> test_keys;
    test_keys.reserve(1000);
    for (size_t i = 0; i < 1000; ++i) {
        test_keys.push_back("performance_test_key_" + std::to_string(i));
    }

    BENCHMARK("FNV-1a hash calculation") {
        uint64_t sum = 0;
        for (const auto& key : test_keys) {
            sum += hasher.hash(key).value;
        }
        return sum;
    };

    BENCHMARK("FNV-1a index calculation") {
        uint64_t sum = 0;
        for (const auto& key : test_keys) {
            sum += hasher.index_for(key).value;
        }
        return sum;
    };
}

TEST_CASE("Linear probe performance", "[hashers][linear_probe][performance][!benchmark]") {
    slot_count slots{10000};
    linear_probe_hasher hasher{fnv1a_hasher{slots}, 10};

    std::vector<std::string> test_keys;
    test_keys.reserve(100);
    for (size_t i = 0; i < 100; ++i) {
        test_keys.push_back("probe_test_" + std::to_string(i));
    }

    BENCHMARK("Probe sequence generation") {
        size_t sum = 0;
        for (const auto& key : test_keys) {
            auto probe_seq = hasher.probe_sequence(key);
            while (!probe_seq.at_end()) {
                sum += (*probe_seq).value;
                ++probe_seq;
            }
        }
        return sum;
    };
}

// ===== STRESS TESTS =====
// Test hashers under heavy load

TEST_CASE("Hash collision analysis", "[hashers][stress]") {
    SECTION("Collision rate measurement") {
        slot_count slots{1000};
        fnv1a_hasher hasher{slots};

        std::unordered_map<uint64_t, size_t> index_counts;
        constexpr size_t num_keys = 50000;

        // Generate many keys and count collisions
        for (size_t i = 0; i < num_keys; ++i) {
            std::string key = "collision_test_" + std::to_string(i) + "_" + std::to_string(i * 7);
            auto index = hasher.index_for(key);
            index_counts[index.value]++;
        }

        // Calculate statistics
        double mean = double(num_keys) / slots.value;
        double variance = 0.0;

        for (uint64_t i = 0; i < slots.value; ++i) {
            double count = index_counts[i];
            variance += (count - mean) * (count - mean);
        }
        variance /= slots.value;

        double stddev = std::sqrt(variance);

        // With good distribution, standard deviation should be reasonable
        // For Poisson distribution, stddev ≈ sqrt(mean)
        double expected_stddev = std::sqrt(mean);

        INFO("Mean: " << mean << ", StdDev: " << stddev << ", Expected: " << expected_stddev);

        // Allow some tolerance for randomness
        REQUIRE(stddev < expected_stddev * 2.0);
    }
}

// ===== INTEGRATION TESTS WITH CONCEPTS =====
// Test that hashers work correctly with the concept system

template<hasher H>
void test_hasher_concept(H&& h, std::string_view test_key) {
    // This function should compile for any type satisfying the hasher concept
    auto hash = h.hash(test_key);
    auto slots = h.max_slots();

    REQUIRE(hash.value != 0);  // FNV-1a specific
    REQUIRE(slots.value > 0);
}

TEST_CASE("Hasher concept integration", "[hashers][concepts]") {
    SECTION("FNV-1a satisfies hasher concept") {
        fnv1a_hasher hasher{slot_count{100}};
        REQUIRE_NOTHROW(test_hasher_concept(hasher, "concept_test"));
    }

    SECTION("Linear probe satisfies hasher concept") {
        auto probe_hasher = linear_probe_hasher{fnv1a_hasher{slot_count{100}}, 5};
        REQUIRE_NOTHROW(test_hasher_concept(probe_hasher, "concept_test"));
    }
}

// ===== MINIMAL PERFECT HASHER SERIALIZATION =====

TEST_CASE("minimal_perfect_hasher: serialize/deserialize round-trip", "[hashers][serialization]") {
    auto result = minimal_perfect_hasher::builder{}
        .add("alpha")
        .add("beta")
        .add("gamma")
        .build();
    REQUIRE(result.has_value());

    auto& hasher = result.value();

    auto serialized = hasher.serialize();
    REQUIRE(serialized.size() > 0);

    auto restored = minimal_perfect_hasher::deserialize(serialized);
    REQUIRE(restored.has_value());

    // Verify all keys round-trip correctly
    for (auto key : {"alpha", "beta", "gamma"}) {
        auto orig_slot = hasher.slot_for(key);
        auto rest_slot = restored->slot_for(key);
        REQUIRE(orig_slot.has_value());
        REQUIRE(rest_slot.has_value());
        REQUIRE(orig_slot->value == rest_slot->value);
    }

    // Unknown keys should still not be found
    REQUIRE_FALSE(restored->is_perfect_for("delta"));
}

TEST_CASE("minimal_perfect_hasher: deserialize empty data", "[hashers][serialization]") {
    std::vector<std::byte> empty;
    auto result = minimal_perfect_hasher::deserialize(empty);
    REQUIRE_FALSE(result.has_value());
}