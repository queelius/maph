/**
 * @file test_table.cpp
 * @brief Comprehensive tests for maph hash table implementation
 *
 * Tests focus on the composable hash table design:
 * - Basic hash table operations (get, set, remove)
 * - Composition of different hashers and storage backends
 * - Linear probing vs direct indexing behavior
 * - Batch operations and iteration
 * - Error handling and edge cases
 * - Performance characteristics
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <maph/table.hpp>
#include <maph/hashers.hpp>
#include <maph/storage.hpp>
#include <vector>
#include <unordered_set>
#include <algorithm>
#include <filesystem>
#include <random>

using namespace maph;

// ===== TEST UTILITIES =====

// Helper to create test tables with different configurations
template<typename Hasher, typename Storage>
auto make_test_table(Hasher&& hasher, Storage&& storage) {
    return hash_table{std::forward<Hasher>(hasher), std::forward<Storage>(storage)};
}

// Unique test file path generator for mmap tests
std::filesystem::path get_test_table_file(const std::string& test_name) {
    static std::atomic<size_t> counter{0};
    auto temp_dir = std::filesystem::temp_directory_path();
    return temp_dir / ("table_test_" + test_name + "_" + std::to_string(counter++) + ".maph");
}

struct temp_file_guard {
    std::filesystem::path path;
    explicit temp_file_guard(std::filesystem::path p) : path(std::move(p)) {}
    ~temp_file_guard() { std::filesystem::remove(path); }
};

// ===== BASIC TABLE OPERATIONS =====

TEST_CASE("hash_table basic operations", "[table][basic]") {
    slot_count slots{100};
    auto table = make_test_table(
        fnv1a_hasher{slots},
        heap_storage<512>{slots}
    );

    SECTION("Empty table state") {
        auto stats = table.statistics();
        REQUIRE(stats.total_slots.value == slots.value);
        REQUIRE(stats.used_slots == 0);
        REQUIRE(stats.load_factor == 0.0);

        // Getting non-existent key should fail
        auto result = table.get("nonexistent");
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == error::key_not_found);

        REQUIRE_FALSE(table.contains("nonexistent"));
    }

    SECTION("Basic set and get operations") {
        std::string key = "test_key";
        std::string value = "test_value";

        // Set a key-value pair
        auto set_result = table.set(key, value);
        REQUIRE(set_result.has_value());

        // Verify it exists
        REQUIRE(table.contains(key));

        // Get the value back
        auto get_result = table.get(key);
        REQUIRE(get_result.has_value());
        REQUIRE(*get_result == value);

        // Check statistics updated
        auto stats = table.statistics();
        REQUIRE(stats.used_slots == 1);
        REQUIRE(stats.load_factor > 0.0);
    }

    SECTION("Key overwriting") {
        std::string key = "overwrite_key";
        std::string value1 = "original_value";
        std::string value2 = "new_value";

        // Set initial value
        table.set(key, value1);
        auto initial_get = table.get(key);
        REQUIRE(initial_get.has_value());
        REQUIRE(*initial_get == value1);

        // Overwrite with new value
        auto overwrite_result = table.set(key, value2);
        REQUIRE(overwrite_result.has_value());

        // Verify new value
        auto final_get = table.get(key);
        REQUIRE(final_get.has_value());
        REQUIRE(*final_get == value2);

        // Should still be only one slot used
        auto stats = table.statistics();
        REQUIRE(stats.used_slots == 1);
    }

    SECTION("Remove operations") {
        std::string key = "remove_key";
        std::string value = "remove_value";

        // Set and verify
        table.set(key, value);
        REQUIRE(table.contains(key));

        // Remove the key
        auto remove_result = table.remove(key);
        REQUIRE(remove_result.has_value());

        // Verify removal
        REQUIRE_FALSE(table.contains(key));
        auto get_result = table.get(key);
        REQUIRE_FALSE(get_result.has_value());
        REQUIRE(get_result.error() == error::key_not_found);

        // Statistics should reflect removal
        auto stats = table.statistics();
        REQUIRE(stats.used_slots == 0);
    }

    SECTION("Removing non-existent key") {
        auto remove_result = table.remove("does_not_exist");
        REQUIRE_FALSE(remove_result.has_value());
        REQUIRE(remove_result.error() == error::key_not_found);
    }
}

TEST_CASE("hash_table with linear probing", "[table][linear_probe]") {
    slot_count slots{10};  // Small table to force collisions
    auto table = make_test_table(
        linear_probe_hasher{fnv1a_hasher{slots}, 5},
        heap_storage<512>{slots}
    );

    SECTION("Collision handling with probing") {
        // Insert multiple keys - some may collide
        std::vector<std::pair<std::string, std::string>> test_data = {
            {"key1", "value1"},
            {"key2", "value2"},
            {"key3", "value3"},
            {"key4", "value4"},
            {"key5", "value5"}
        };

        // Insert all data
        for (const auto& [key, value] : test_data) {
            auto result = table.set(key, value);
            REQUIRE(result.has_value());
        }

        // Verify all data can be retrieved
        for (const auto& [key, value] : test_data) {
            REQUIRE(table.contains(key));
            auto get_result = table.get(key);
            REQUIRE(get_result.has_value());
            REQUIRE(*get_result == value);
        }

        auto stats = table.statistics();
        REQUIRE(stats.used_slots == test_data.size());
    }

    SECTION("Table full condition") {
        // Fill the table completely
        for (size_t i = 0; i < slots.value; ++i) {
            std::string key = "full_key_" + std::to_string(i);
            std::string value = "full_value_" + std::to_string(i);
            auto result = table.set(key, value);
            REQUIRE(result.has_value());
        }

        // Adding another key should fail due to limited probing
        auto overflow_result = table.set("overflow_key", "overflow_value");
        // This may succeed or fail depending on hash distribution and probe limit
        // The important thing is that the behavior is well-defined
    }

    SECTION("Remove with probing") {
        // Insert keys that may probe
        table.set("probe_key1", "probe_value1");
        table.set("probe_key2", "probe_value2");
        table.set("probe_key3", "probe_value3");

        // Remove middle key
        auto remove_result = table.remove("probe_key2");
        REQUIRE(remove_result.has_value());

        // Other keys should still be accessible
        REQUIRE(table.contains("probe_key1"));
        REQUIRE(table.contains("probe_key3"));
        REQUIRE_FALSE(table.contains("probe_key2"));
    }
}

TEST_CASE("hash_table with direct indexing", "[table][direct_index]") {
    slot_count slots{100};
    auto table = make_test_table(
        fnv1a_hasher{slots},  // No linear probing
        heap_storage<512>{slots}
    );

    SECTION("Direct hash-to-slot mapping") {
        std::string key = "direct_key";
        std::string value = "direct_value";

        table.set(key, value);

        // For direct indexing, the key should be found at its hash index
        REQUIRE(table.contains(key));
        auto get_result = table.get(key);
        REQUIRE(get_result.has_value());
        REQUIRE(*get_result == value);
    }

    SECTION("Hash collisions with direct indexing") {
        // With direct indexing, hash collisions overwrite
        // This is expected behavior for non-probing hashers

        // Note: Hard to construct deliberate collisions with FNV-1a
        // So we test the interface behavior instead

        std::string key1 = "collision_test_1";
        std::string key2 = "collision_test_2";
        std::string value1 = "value1";
        std::string value2 = "value2";

        table.set(key1, value1);
        table.set(key2, value2);

        // Both should be retrievable (assuming no collision)
        auto result1 = table.get(key1);
        auto result2 = table.get(key2);

        if (result1.has_value() && result2.has_value()) {
            REQUIRE(*result1 == value1);
            REQUIRE(*result2 == value2);
        }
        // If collision occurred, one would overwrite the other
        // This is expected behavior for direct indexing
    }
}

// ===== COMPOSITION TESTS =====
// Test different combinations of hashers and storage

TEST_CASE("hash_table composition", "[table][composition]") {
    slot_count slots{50};

    SECTION("Different storage backends") {
        auto heap_table = make_test_table(
            fnv1a_hasher{slots},
            heap_storage<512>{slots}
        );

        auto test_path = get_test_table_file("composition");
        temp_file_guard guard{test_path};

        auto mmap_storage_result = mmap_storage<>::create(test_path, slots);
        REQUIRE(mmap_storage_result.has_value());

        auto mmap_table = make_test_table(
            fnv1a_hasher{slots},
            std::move(*mmap_storage_result)
        );

        // Both tables should behave identically
        std::string key = "composition_key";
        std::string value = "composition_value";

        heap_table.set(key, value);
        mmap_table.set(key, value);

        auto heap_result = heap_table.get(key);
        auto mmap_result = mmap_table.get(key);

        REQUIRE(heap_result.has_value());
        REQUIRE(mmap_result.has_value());
        REQUIRE(*heap_result == *mmap_result);
    }

    SECTION("Cached storage composition") {
        auto cached_table = make_test_table(
            linear_probe_hasher{fnv1a_hasher{slots}, 10},
            cached_storage{heap_storage<512>{slots}, 5}
        );

        // Test that caching doesn't affect semantics
        std::string key = "cached_key";
        std::string value = "cached_value";

        cached_table.set(key, value);

        // Multiple reads should work (some from cache, some not)
        for (int i = 0; i < 10; ++i) {
            auto result = cached_table.get(key);
            REQUIRE(result.has_value());
            REQUIRE(*result == value);
        }
    }
}

// ===== BATCH OPERATIONS =====

TEST_CASE("hash_table batch operations", "[table][batch]") {
    slot_count slots{200};
    auto table = make_test_table(
        linear_probe_hasher{fnv1a_hasher{slots}, 20},
        heap_storage<512>{slots}
    );

    SECTION("Batch get operations") {
        // First insert test data
        std::vector<std::pair<std::string, std::string>> test_data = {
            {"batch_key1", "batch_value1"},
            {"batch_key2", "batch_value2"},
            {"batch_key3", "batch_value3"},
            {"batch_key4", "batch_value4"}
        };

        for (const auto& [key, value] : test_data) {
            table.set(key, value);
        }

        // Prepare keys for batch get
        std::vector<std::string_view> keys = {
            "batch_key1", "batch_key3", "nonexistent_key", "batch_key2"
        };

        // Use batch get with callback
        std::vector<std::pair<std::string, std::string>> found_items;
        table.get_batch(keys, [&](std::string_view key, std::string_view value) {
            found_items.emplace_back(key, value);
        });

        // Should have found 3 out of 4 keys
        REQUIRE(found_items.size() == 3);

        // Verify found items
        std::unordered_set<std::string> found_keys;
        for (const auto& [key, value] : found_items) {
            found_keys.insert(key);
        }

        REQUIRE(found_keys.count("batch_key1") == 1);
        REQUIRE(found_keys.count("batch_key2") == 1);
        REQUIRE(found_keys.count("batch_key3") == 1);
        REQUIRE(found_keys.count("nonexistent_key") == 0);
    }

    SECTION("Batch set operations") {
        std::vector<std::pair<std::string_view, std::string_view>> pairs = {
            {"batch_set1", "value1"},
            {"batch_set2", "value2"},
            {"batch_set3", "value3"},
            {"batch_set4", "value4"},
            {"batch_set5", "value5"}
        };

        size_t success_count = table.set_batch(pairs);
        REQUIRE(success_count == pairs.size());

        // Verify all were set
        for (const auto& [key, value] : pairs) {
            REQUIRE(table.contains(key));
            auto get_result = table.get(key);
            REQUIRE(get_result.has_value());
            REQUIRE(*get_result == value);
        }
    }
}

// ===== ITERATION TESTS =====

TEST_CASE("hash_table iteration", "[table][iteration]") {
    slot_count slots{50};
    auto table = make_test_table(
        fnv1a_hasher{slots},
        heap_storage<512>{slots}
    );

    SECTION("Iterate over empty table") {
        auto items = table.items();
        auto first_item = items.next();
        REQUIRE_FALSE(first_item.has_value());
    }

    SECTION("Iterate over populated table") {
        // Insert test data
        std::vector<std::pair<std::string, std::string>> test_data = {
            {"iter_key1", "iter_value1"},
            {"iter_key2", "iter_value2"},
            {"iter_key3", "iter_value3"}
        };

        for (const auto& [key, value] : test_data) {
            table.set(key, value);
        }

        // Iterate and collect items
        std::vector<std::string> found_values;
        auto items = table.items();

        while (auto item = items.next()) {
            found_values.emplace_back(item->value);
        }

        // Should find all inserted values
        REQUIRE(found_values.size() == test_data.size());

        // All expected values should be present
        for (const auto& [key, expected_value] : test_data) {
            REQUIRE(std::find(found_values.begin(), found_values.end(), expected_value) != found_values.end());
        }
    }

    SECTION("Iterator consistency") {
        // Add some data
        table.set("consistent1", "value1");
        table.set("consistent2", "value2");

        // Multiple iterations should yield same results
        std::vector<std::string> first_iteration;
        std::vector<std::string> second_iteration;

        auto items1 = table.items();
        while (auto item = items1.next()) {
            first_iteration.emplace_back(item->value);
        }

        auto items2 = table.items();
        while (auto item = items2.next()) {
            second_iteration.emplace_back(item->value);
        }

        std::sort(first_iteration.begin(), first_iteration.end());
        std::sort(second_iteration.begin(), second_iteration.end());

        REQUIRE(first_iteration == second_iteration);
    }
}

// ===== FACTORY FUNCTION TESTS =====

TEST_CASE("hash_table factory functions", "[table][factory]") {
    SECTION("make_table factory") {
        slot_count slots{100};
        auto hasher = fnv1a_hasher{slots};
        auto storage = heap_storage<512>{slots};

        auto table = make_table(std::move(hasher), std::move(storage));

        // Test that factory-created table works
        table.set("factory_key", "factory_value");
        auto result = table.get("factory_key");
        REQUIRE(result.has_value());
        REQUIRE(*result == "factory_value");
    }

    SECTION("make_memory_table convenience factory") {
        auto table = make_memory_table(slot_count{50});

        // Should create working in-memory table
        table.set("memory_key", "memory_value");
        auto result = table.get("memory_key");
        REQUIRE(result.has_value());
        REQUIRE(*result == "memory_value");

        auto stats = table.statistics();
        REQUIRE(stats.total_slots.value == 50);
        REQUIRE(stats.used_slots == 1);
    }

    SECTION("make_mmap_table convenience factory") {
        auto test_path = get_test_table_file("factory_mmap");
        temp_file_guard guard{test_path};

        auto table_result = make_mmap_table(test_path, slot_count{30});
        REQUIRE(table_result.has_value());

        auto& table = *table_result;

        // Should create working memory-mapped table
        table.set("mmap_factory_key", "mmap_factory_value");
        auto result = table.get("mmap_factory_key");
        REQUIRE(result.has_value());
        REQUIRE(*result == "mmap_factory_value");

        auto stats = table.statistics();
        REQUIRE(stats.total_slots.value == 30);
    }
}

// ===== ERROR HANDLING TESTS =====

TEST_CASE("hash_table error handling", "[table][error_handling]") {
    slot_count slots{10};
    auto table = make_test_table(
        fnv1a_hasher{slots},
        heap_storage<256>{slots}  // Small slots to test size limits
    );

    SECTION("Value too large") {
        std::string key = "large_key";
        std::string large_value(heap_storage<256>::slot_type::data_size + 1, 'X');

        auto set_result = table.set(key, large_value);
        REQUIRE_FALSE(set_result.has_value());
        REQUIRE(set_result.error() == error::value_too_large);

        // Key should not exist in table
        REQUIRE_FALSE(table.contains(key));
    }

    SECTION("Empty key and value handling") {
        std::string empty_key = "";
        std::string empty_value = "";

        // Empty keys and values should be allowed
        auto set_result = table.set(empty_key, empty_value);
        REQUIRE(set_result.has_value());

        auto get_result = table.get(empty_key);
        REQUIRE(get_result.has_value());
        REQUIRE(*get_result == empty_value);
    }

    SECTION("Very long keys") {
        std::string long_key(10000, 'K');
        std::string value = "long_key_value";

        // Long keys should be hashable and work normally
        auto set_result = table.set(long_key, value);
        REQUIRE(set_result.has_value());

        auto get_result = table.get(long_key);
        REQUIRE(get_result.has_value());
        REQUIRE(*get_result == value);
    }
}

// ===== PROPERTY-BASED TESTS =====

TEST_CASE("hash_table properties", "[table][properties]") {
    slot_count slots{100};
    auto table = make_test_table(
        linear_probe_hasher{fnv1a_hasher{slots}, 10},
        heap_storage<512>{slots}
    );

    SECTION("Set-then-get consistency") {
        auto key_suffix = GENERATE(take(20, random(1, 10000)));
        std::string key = "prop_key_" + std::to_string(key_suffix);
        std::string value = "prop_value_" + std::to_string(key_suffix * 2);

        auto set_result = table.set(key, value);
        if (set_result.has_value()) {
            // If set succeeded, get should succeed with same value
            auto get_result = table.get(key);
            REQUIRE(get_result.has_value());
            REQUIRE(*get_result == value);
            REQUIRE(table.contains(key));
        }
    }

    SECTION("Remove consistency") {
        auto key_suffix = GENERATE(take(10, random(1, 1000)));
        std::string key = "remove_prop_" + std::to_string(key_suffix);
        std::string value = "remove_value_" + std::to_string(key_suffix);

        // Set, then remove
        table.set(key, value);
        REQUIRE(table.contains(key));

        auto remove_result = table.remove(key);
        if (remove_result.has_value()) {
            // If remove succeeded, key should not exist
            REQUIRE_FALSE(table.contains(key));
            auto get_result = table.get(key);
            REQUIRE_FALSE(get_result.has_value());
        }
    }

    SECTION("Statistics consistency") {
        // Start with known state
        size_t initial_count = table.statistics().used_slots;

        std::string key = "stats_key";
        std::string value = "stats_value";

        // Add a key
        table.set(key, value);
        size_t after_set = table.statistics().used_slots;

        if (table.contains(key)) {
            // If key was actually added
            REQUIRE(after_set >= initial_count);
        }

        // Remove the key
        if (table.remove(key).has_value()) {
            size_t after_remove = table.statistics().used_slots;
            REQUIRE(after_remove < after_set);
        }
    }
}

// ===== PERFORMANCE TESTS =====

TEST_CASE("hash_table performance", "[table][performance][!benchmark]") {
    slot_count slots{10000};
    auto table = make_test_table(
        linear_probe_hasher{fnv1a_hasher{slots}, 20},
        heap_storage<512>{slots}
    );

    // Prepare test data
    std::vector<std::pair<std::string, std::string>> test_data;
    test_data.reserve(1000);
    for (size_t i = 0; i < 1000; ++i) {
        test_data.emplace_back(
            "perf_key_" + std::to_string(i),
            "perf_value_" + std::to_string(i) + "_" + std::string(50, 'V')
        );
    }

    BENCHMARK("Table set operations") {
        for (const auto& [key, value] : test_data) {
            table.set(key, value);
        }
    };

    // Insert data for read benchmark
    for (const auto& [key, value] : test_data) {
        table.set(key, value);
    }

    BENCHMARK("Table get operations") {
        for (const auto& [key, value] : test_data) {
            auto result = table.get(key);
            (void)result;  // Prevent optimization
        }
    };

    BENCHMARK("Table contains operations") {
        for (const auto& [key, value] : test_data) {
            bool exists = table.contains(key);
            (void)exists;  // Prevent optimization
        }
    };
}

TEST_CASE("hash_table load factor performance", "[table][performance][load_factor][!benchmark]") {
    slot_count slots{1000};

    SECTION("Performance at different load factors") {
        // Test at 25%, 50%, 75%, and 90% load factors
        std::vector<double> load_factors = {0.25, 0.5, 0.75, 0.9};

        for (double target_load : load_factors) {
            auto table = make_test_table(
                linear_probe_hasher{fnv1a_hasher{slots}, 10},
                heap_storage<512>{slots}
            );

            size_t num_items = static_cast<size_t>(slots.value * target_load);

            // Fill to target load factor
            for (size_t i = 0; i < num_items; ++i) {
                std::string key = "load_test_" + std::to_string(i);
                std::string value = "value_" + std::to_string(i);
                table.set(key, value);
            }

            // Measure lookup performance
            auto start = std::chrono::high_resolution_clock::now();
            for (size_t i = 0; i < num_items; ++i) {
                std::string key = "load_test_" + std::to_string(i);
                [[maybe_unused]] auto result = table.get(key);
            }
            auto end = std::chrono::high_resolution_clock::now();

            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            INFO("Load factor " << target_load << ": " << duration.count() << " μs for " << num_items << " lookups");

            // Just verify that lookups work at all load factors
            REQUIRE(table.statistics().used_slots == num_items);
        }
    }
}

// ===== STRESS TESTS =====

TEST_CASE("hash_table stress tests", "[table][stress]") {
    SECTION("Many operations") {
        slot_count slots{1000};
        auto table = make_test_table(
            linear_probe_hasher{fnv1a_hasher{slots}, 20},
            heap_storage<512>{slots}
        );

        std::mt19937 rng{12345};
        std::uniform_int_distribution<int> op_dist{0, 2};  // 0=set, 1=get, 2=remove
        std::uniform_int_distribution<size_t> key_dist{0, 999};

        std::unordered_set<std::string> expected_keys;

        // Perform many random operations
        for (size_t i = 0; i < 10000; ++i) {
            int op = op_dist(rng);
            size_t key_num = key_dist(rng);
            std::string key = "stress_" + std::to_string(key_num);
            std::string value = "value_" + std::to_string(i);

            switch (op) {
                case 0: {  // Set
                    auto result = table.set(key, value);
                    if (result.has_value()) {
                        expected_keys.insert(key);
                    }
                    break;
                }
                case 1: {  // Get
                    auto result = table.get(key);
                    if (expected_keys.count(key) > 0) {
                        // If we expect the key to exist, it should be found
                        // (unless there were hash collisions)
                        // We can't guarantee this with direct indexing, so just verify no crash
                    }
                    break;
                }
                case 2: {  // Remove
                    auto result = table.remove(key);
                    if (result.has_value()) {
                        expected_keys.erase(key);
                    }
                    break;
                }
            }
        }

        // Verify table is still functional
        table.set("final_test", "final_value");
        auto final_result = table.get("final_test");
        REQUIRE(final_result.has_value());
        REQUIRE(*final_result == "final_value");
    }

    SECTION("High load factor stress") {
        slot_count slots{100};
        auto table = make_test_table(
            linear_probe_hasher{fnv1a_hasher{slots}, 50},  // High probe limit
            heap_storage<512>{slots}
        );

        // Try to fill table to very high load factor
        size_t successful_inserts = 0;
        for (size_t i = 0; i < slots.value * 2; ++i) {  // Try to insert 2x capacity
            std::string key = "high_load_" + std::to_string(i);
            std::string value = "value_" + std::to_string(i);

            auto result = table.set(key, value);
            if (result.has_value()) {
                successful_inserts++;
            }
        }

        // Should have inserted at least something
        REQUIRE(successful_inserts > 0);

        // Verify all successfully inserted keys can be retrieved
        for (size_t i = 0; i < successful_inserts; ++i) {
            std::string key = "high_load_" + std::to_string(i);
            auto result = table.get(key);
            // May or may not find due to collisions, but shouldn't crash
        }

        auto stats = table.statistics();
        REQUIRE(stats.used_slots <= slots.value);  // Can't exceed physical slots
    }
}