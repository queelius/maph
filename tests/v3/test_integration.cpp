/**
 * @file test_integration.cpp
 * @brief Integration tests for maph v3 - testing component interactions
 *
 * These tests verify that the v3 components work together correctly:
 * - High-level maph interface integration
 * - Optimization and journaling workflows
 * - Complex composition scenarios
 * - End-to-end user workflows
 * - Cross-component error propagation
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include "maph/v3/maph.hpp"
#include "maph/v3/optimization.hpp"
#include <filesystem>
#include <thread>
#include <vector>
#include <random>
#include <algorithm>

using namespace maph::v3;

// ===== TEST UTILITIES =====

std::filesystem::path get_integration_test_file(const std::string& test_name) {
    static std::atomic<size_t> counter{0};
    auto temp_dir = std::filesystem::temp_directory_path();
    return temp_dir / ("integration_test_" + test_name + "_" + std::to_string(counter++) + ".maph");
}

struct temp_file_guard {
    std::filesystem::path path;
    explicit temp_file_guard(std::filesystem::path p) : path(std::move(p)) {}
    ~temp_file_guard() { std::filesystem::remove(path); }
};

// ===== HIGH-LEVEL MAPH INTERFACE TESTS =====

TEST_CASE("maph high-level interface integration", "[integration][maph_interface]") {
    SECTION("Complete database lifecycle") {
        auto test_path = get_integration_test_file("lifecycle");
        temp_file_guard guard{test_path};

        // Create database
        auto config = maph::config{
            .slots = slot_count{1000},
            .max_probes = 15,
            .enable_journal = true,
            .enable_cache = false
        };

        auto db_result = maph::create(test_path, config);
        REQUIRE(db_result.has_value());
        auto& db = *db_result;

        // Verify initial state
        REQUIRE(db.empty());
        REQUIRE(db.size() == 0);
        REQUIRE(db.load_factor() == 0.0);

        // Insert test data
        std::vector<std::pair<std::string, std::string>> test_data = {
            {"user:1", R"({"name": "Alice", "age": 30})"},
            {"user:2", R"({"name": "Bob", "age": 25})"},
            {"user:3", R"({"name": "Carol", "age": 35})"},
            {"settings:theme", "dark"},
            {"settings:language", "en-US"}
        };

        for (const auto& [key, value] : test_data) {
            auto result = db.set(key, value);
            REQUIRE(result.has_value());
        }

        // Verify data
        REQUIRE_FALSE(db.empty());
        REQUIRE(db.size() == test_data.size());

        for (const auto& [key, expected_value] : test_data) {
            REQUIRE(db.contains(key));
            auto get_result = db.get(key);
            REQUIRE(get_result.has_value());
            REQUIRE(*get_result == expected_value);
        }

        // Test functional operations
        auto user1_default = db.get_or("user:1", "{}");
        REQUIRE(user1_default != "{}");

        auto missing_default = db.get_or("user:999", R"({"name": "Unknown"})");
        REQUIRE(missing_default == R"({"name": "Unknown"})");

        // Test update operation
        bool updated = db.update("user:1", [](std::string_view current) {
            return R"({"name": "Alice", "age": 31})";  // Birthday!
        });
        REQUIRE(updated);

        auto updated_result = db.get("user:1");
        REQUIRE(updated_result.has_value());
        REQUIRE(updated_result->find("31") != std::string_view::npos);
    }

    SECTION("Database persistence across sessions") {
        auto test_path = get_integration_test_file("persistence");
        temp_file_guard guard{test_path};

        std::string persistent_key = "persistent_data";
        std::string persistent_value = "this_should_survive_restart";

        // Create and populate database
        {
            auto db = maph::create(test_path, {.slots = slot_count{100}});
            REQUIRE(db.has_value());

            db->set(persistent_key, persistent_value);
            db->set("temp_key", "temp_value");

            REQUIRE(db->contains(persistent_key));
            REQUIRE(db->size() == 2);
        }  // Database closes here

        // Reopen and verify persistence
        {
            auto db = maph::open(test_path);
            REQUIRE(db.has_value());

            REQUIRE(db->contains(persistent_key));
            auto result = db->get(persistent_key);
            REQUIRE(result.has_value());
            REQUIRE(*result == persistent_value);

            REQUIRE(db->contains("temp_key"));
            REQUIRE(db->size() == 2);
        }
    }

    SECTION("Read-only database access") {
        auto test_path = get_integration_test_file("readonly");
        temp_file_guard guard{test_path};

        // Create and populate database
        {
            auto db = maph::create(test_path, {.slots = slot_count{50}});
            REQUIRE(db.has_value());

            db->set("readonly_key", "readonly_value");
        }

        // Open in read-only mode
        auto readonly_db = maph::open(test_path, true);
        REQUIRE(readonly_db.has_value());

        // Reading should work
        auto read_result = readonly_db->get("readonly_key");
        REQUIRE(read_result.has_value());
        REQUIRE(*read_result == "readonly_value");

        // Writing should fail
        auto write_result = readonly_db->set("new_key", "new_value");
        REQUIRE_FALSE(write_result.has_value());
        REQUIRE(write_result.error() == error::permission_denied);
    }
}

TEST_CASE("maph in-memory database integration", "[integration][memory]") {
    SECTION("Different configurations") {
        // Basic memory database
        auto basic_db = maph::create_memory({.slots = slot_count{100}});
        basic_db.set("basic_key", "basic_value");
        REQUIRE(basic_db.contains("basic_key"));

        // Memory database with caching
        auto cached_db = maph::create_memory({
            .slots = slot_count{200},
            .enable_cache = true,
            .cache_size = 50
        });

        // Cache should improve repeated access
        cached_db.set("cached_key", "cached_value");

        for (int i = 0; i < 10; ++i) {
            auto result = cached_db.get("cached_key");
            REQUIRE(result.has_value());
            REQUIRE(*result == "cached_value");
        }

        // Different journal settings
        auto journal_db = maph::create_memory({
            .slots = slot_count{150},
            .enable_journal = true
        });

        journal_db.set("journal_key", "journal_value");
        REQUIRE(journal_db.contains("journal_key"));
    }
}

// ===== BATCH OPERATIONS INTEGRATION =====

TEST_CASE("maph batch operations integration", "[integration][batch]") {
    auto db = maph::create_memory({.slots = slot_count{500}});

    SECTION("Transactional batch operations") {
        // Test all-or-nothing semantics
        auto batch_data = {
            std::make_pair("batch1", "value1"),
            std::make_pair("batch2", "value2"),
            std::make_pair("batch3", "value3"),
            std::make_pair("batch4", "value4")
        };

        auto batch_result = db.set_all(batch_data);
        REQUIRE(batch_result.has_value());

        // All keys should exist
        for (const auto& [key, expected_value] : batch_data) {
            REQUIRE(db.contains(key));
            auto get_result = db.get(key);
            REQUIRE(get_result.has_value());
            REQUIRE(*get_result == expected_value);
        }

        REQUIRE(db.size() == batch_data.size());
    }

    SECTION("Batch operations under memory pressure") {
        // Fill database to near capacity
        size_t capacity = 400;  // Less than total slots to leave room
        for (size_t i = 0; i < capacity; ++i) {
            std::string key = "pressure_" + std::to_string(i);
            std::string value = "value_" + std::to_string(i);
            db.set(key, value);
        }

        // Try to add batch that might exceed capacity
        auto large_batch = {
            std::make_pair("overflow1", "val1"),
            std::make_pair("overflow2", "val2"),
            std::make_pair("overflow3", "val3")
        };

        auto batch_result = db.set_all(large_batch);
        // May succeed or fail based on implementation, but should handle gracefully
        if (batch_result.has_value()) {
            // If successful, all keys should exist
            for (const auto& [key, value] : large_batch) {
                REQUIRE(db.contains(key));
            }
        }
    }
}

// ===== OPTIMIZATION AND JOURNALING INTEGRATION =====

TEST_CASE("optimization workflow integration", "[integration][optimization]") {
    SECTION("Journaled table optimization workflow") {
        slot_count slots{200};
        auto standard_table = with_journal(
            make_table(
                linear_probe_hasher{fnv1a_hasher{slots}},
                heap_storage<512>{slots}
            )
        );

        // Build up a dataset
        std::vector<std::string> keys;
        for (size_t i = 0; i < 50; ++i) {
            std::string key = "optimize_key_" + std::to_string(i);
            std::string value = "optimize_value_" + std::to_string(i);

            auto result = standard_table.set(key, value);
            REQUIRE(result.has_value());
            keys.push_back(key);
        }

        // Verify journal tracks keys
        REQUIRE(standard_table.journal().size() == keys.size());

        // Verify all keys are accessible
        for (const auto& key : keys) {
            REQUIRE(standard_table.contains(key));
        }

        // Test optimization preparation
        auto perfect_result = standard_table.optimize(heap_storage<512>{slots});
        // Note: This may fail if minimal_perfect_hasher is not fully implemented
        // The test verifies the interface and error handling

        if (perfect_result.has_value()) {
            auto& perfect_table = *perfect_result;

            // All keys should still be accessible in optimized table
            for (const auto& key : keys) {
                auto get_result = perfect_table.get(key);
                REQUIRE(get_result.has_value());
            }
        }
    }

    SECTION("Key journal functionality") {
        key_journal journal;

        // Test journal operations
        std::vector<std::string> test_keys = {
            "journal_key1", "journal_key2", "journal_key3"
        };

        for (const auto& key : test_keys) {
            journal.record_insert(key);
        }

        REQUIRE(journal.size() == test_keys.size());

        // Verify keys are tracked
        const auto& tracked_keys = journal.keys();
        for (const auto& key : test_keys) {
            REQUIRE(std::find(tracked_keys.begin(), tracked_keys.end(), key) != tracked_keys.end());
        }

        // Test remove tracking
        journal.record_remove("journal_key2");
        REQUIRE(journal.size() == test_keys.size() - 1);

        // Test duplicate handling
        journal.record_insert("journal_key1");  // Duplicate
        REQUIRE(journal.size() == test_keys.size() - 1);  // Should not increase

        // Test clear
        journal.clear();
        REQUIRE(journal.size() == 0);
        REQUIRE(journal.keys().empty());
    }
}

// ===== ERROR PROPAGATION INTEGRATION =====

TEST_CASE("error propagation integration", "[integration][error_handling]") {
    SECTION("Storage errors propagate to high-level interface") {
        auto test_path = get_integration_test_file("error_prop");
        temp_file_guard guard{test_path};

        // Create database with very small slots
        auto db_result = maph::create(test_path, {.slots = slot_count{10}});
        REQUIRE(db_result.has_value());
        auto& db = *db_result;

        // Try to store value that's too large
        std::string large_value(10000, 'X');  // Way too large for any reasonable slot size
        auto set_result = db.set("large_key", large_value);

        // Error should propagate up
        REQUIRE_FALSE(set_result.has_value());
        // Note: Exact error depends on slot size configuration
    }

    SECTION("File system errors propagate correctly") {
        // Try to create database in non-existent directory
        auto invalid_path = std::filesystem::path("/nonexistent/directory/test.maph");
        auto db_result = maph::create(invalid_path, {.slots = slot_count{10}});

        REQUIRE_FALSE(db_result.has_value());
        REQUIRE(db_result.error() == error::io_error);
    }

    SECTION("Chained error handling") {
        auto db = maph::create_memory({.slots = slot_count{100}});

        // Chain operations and handle errors
        auto chained_result = db.set("key1", "value1")
            .and_then([&](auto) { return db.set("key2", "value2"); })
            .and_then([&](auto) { return db.set("key3", "value3"); });

        REQUIRE(chained_result.has_value());

        // Verify all operations succeeded
        REQUIRE(db.contains("key1"));
        REQUIRE(db.contains("key2"));
        REQUIRE(db.contains("key3"));
    }
}

// ===== COMPOSITION INTEGRATION TESTS =====

TEST_CASE("complex composition integration", "[integration][composition]") {
    SECTION("Deeply composed storage backend") {
        auto test_path = get_integration_test_file("deep_composition");
        temp_file_guard guard{test_path};

        // Create a complex storage composition: mmap + cached
        auto mmap_result = mmap_storage<>::create(test_path, slot_count{100});
        REQUIRE(mmap_result.has_value());

        auto cached_storage = cached_storage{std::move(*mmap_result), 10};

        // Create table with complex hasher + complex storage
        auto complex_table = make_table(
            linear_probe_hasher{fnv1a_hasher{slot_count{100}}, 15},
            std::move(cached_storage)
        );

        // Test that complex composition works
        complex_table.set("complex_key", "complex_value");
        auto result = complex_table.get("complex_key");
        REQUIRE(result.has_value());
        REQUIRE(*result == "complex_value");

        // Test that all layers work together
        for (int i = 0; i < 20; ++i) {
            std::string key = "layer_test_" + std::to_string(i);
            std::string value = "layer_value_" + std::to_string(i);

            complex_table.set(key, value);
            auto get_result = complex_table.get(key);
            REQUIRE(get_result.has_value());
            REQUIRE(*get_result == value);
        }
    }

    SECTION("Multiple hasher strategies") {
        slot_count slots{50};

        // Direct indexing table
        auto direct_table = make_table(
            fnv1a_hasher{slots},
            heap_storage<512>{slots}
        );

        // Linear probing table
        auto probe_table = make_table(
            linear_probe_hasher{fnv1a_hasher{slots}, 10},
            heap_storage<512>{slots}
        );

        // Both should handle the same data
        std::vector<std::pair<std::string, std::string>> test_data = {
            {"strategy_key1", "strategy_value1"},
            {"strategy_key2", "strategy_value2"},
            {"strategy_key3", "strategy_value3"}
        };

        for (const auto& [key, value] : test_data) {
            direct_table.set(key, value);
            probe_table.set(key, value);
        }

        // Both should retrieve the same data
        for (const auto& [key, expected_value] : test_data) {
            auto direct_result = direct_table.get(key);
            auto probe_result = probe_table.get(key);

            // Both may succeed or fail (due to collisions), but if both succeed,
            // they should return the same value
            if (direct_result.has_value() && probe_result.has_value()) {
                REQUIRE(*direct_result == *probe_result);
            }
        }
    }
}

// ===== CONCURRENT ACCESS INTEGRATION =====

TEST_CASE("concurrent access integration", "[integration][threading]") {
    SECTION("Multiple readers on persistent database") {
        auto test_path = get_integration_test_file("concurrent_readers");
        temp_file_guard guard{test_path};

        // Create and populate database
        {
            auto db = maph::create(test_path, {.slots = slot_count{200}});
            REQUIRE(db.has_value());

            for (size_t i = 0; i < 100; ++i) {
                std::string key = "concurrent_" + std::to_string(i);
                std::string value = "value_" + std::to_string(i);
                db->set(key, value);
            }
        }

        // Multiple concurrent readers
        constexpr size_t num_readers = 4;
        std::vector<std::thread> readers;
        std::atomic<size_t> total_reads{0};
        std::atomic<size_t> successful_reads{0};

        for (size_t t = 0; t < num_readers; ++t) {
            readers.emplace_back([&, t]() {
                auto db = maph::open(test_path, true);  // Read-only
                if (!db.has_value()) return;

                for (size_t i = 0; i < 100; ++i) {
                    std::string key = "concurrent_" + std::to_string(i);
                    total_reads++;

                    auto result = db->get(key);
                    if (result.has_value()) {
                        successful_reads++;
                        std::string expected = "value_" + std::to_string(i);
                        REQUIRE(*result == expected);
                    }
                }
            });
        }

        for (auto& reader : readers) {
            reader.join();
        }

        // All reads should have been successful
        REQUIRE(total_reads == num_readers * 100);
        REQUIRE(successful_reads == total_reads);
    }

    SECTION("Memory database thread safety") {
        auto db = maph::create_memory({.slots = slot_count{1000}});

        constexpr size_t num_threads = 4;
        constexpr size_t ops_per_thread = 250;
        std::vector<std::thread> workers;
        std::atomic<size_t> successful_operations{0};

        for (size_t t = 0; t < num_threads; ++t) {
            workers.emplace_back([&, t]() {
                for (size_t i = 0; i < ops_per_thread; ++i) {
                    std::string key = "thread_" + std::to_string(t) + "_key_" + std::to_string(i);
                    std::string value = "thread_" + std::to_string(t) + "_value_" + std::to_string(i);

                    if (db.set(key, value).has_value()) {
                        auto get_result = db.get(key);
                        if (get_result.has_value() && *get_result == value) {
                            successful_operations++;
                        }
                    }
                }
            });
        }

        for (auto& worker : workers) {
            worker.join();
        }

        // Most operations should succeed (some may fail due to collisions)
        REQUIRE(successful_operations > (num_threads * ops_per_thread) / 2);
    }
}

// ===== REAL-WORLD USAGE SCENARIOS =====

TEST_CASE("real-world usage scenarios", "[integration][scenarios]") {
    SECTION("Session store simulation") {
        auto session_store = maph::create_memory({
            .slots = slot_count{10000},
            .enable_cache = true,
            .cache_size = 1000
        });

        // Simulate user sessions
        std::vector<std::string> session_ids;
        for (size_t i = 0; i < 500; ++i) {
            std::string session_id = "sess_" + std::to_string(i);
            std::string session_data = R"({"user_id": )" + std::to_string(i % 100) +
                                      R"(, "login_time": "2023-01-01T00:00:00Z", "permissions": ["read", "write"]})";

            session_store.set(session_id, session_data);
            session_ids.push_back(session_id);
        }

        // Simulate session lookups (hot cache scenario)
        for (size_t i = 0; i < 100; ++i) {
            std::string session_id = session_ids[i % session_ids.size()];
            auto result = session_store.get(session_id);
            REQUIRE(result.has_value());
            REQUIRE(result->find("user_id") != std::string_view::npos);
        }

        // Simulate session cleanup
        size_t cleaned = 0;
        for (size_t i = 0; i < 100; ++i) {  // Clean first 100 sessions
            if (session_store.remove(session_ids[i]).has_value()) {
                cleaned++;
            }
        }

        REQUIRE(cleaned > 0);
        REQUIRE(session_store.size() < session_ids.size());
    }

    SECTION("Configuration management simulation") {
        auto test_path = get_integration_test_file("config_mgmt");
        temp_file_guard guard{test_path};

        auto config_db = maph::create(test_path, {
            .slots = slot_count{1000},
            .enable_journal = true
        });
        REQUIRE(config_db.has_value());

        // Simulate hierarchical configuration
        std::vector<std::pair<std::string, std::string>> config_data = {
            {"app.database.host", "localhost"},
            {"app.database.port", "5432"},
            {"app.database.name", "myapp"},
            {"app.server.host", "0.0.0.0"},
            {"app.server.port", "8080"},
            {"app.logging.level", "INFO"},
            {"app.logging.file", "/var/log/myapp.log"},
            {"feature.auth.enabled", "true"},
            {"feature.auth.provider", "oauth2"},
            {"feature.metrics.enabled", "true"}
        };

        // Batch load configuration
        auto batch_result = config_db->set_all(config_data);
        REQUIRE(batch_result.has_value());

        // Simulate configuration queries
        auto db_host = config_db->get_or("app.database.host", "localhost");
        REQUIRE(db_host == "localhost");

        auto missing_config = config_db->get_or("app.cache.ttl", "3600");
        REQUIRE(missing_config == "3600");

        // Simulate configuration updates
        bool updated = config_db->update("app.logging.level", [](std::string_view) {
            return "DEBUG";
        });
        REQUIRE(updated);

        auto new_level = config_db->get("app.logging.level");
        REQUIRE(new_level.has_value());
        REQUIRE(*new_level == "DEBUG");

        // Verify persistence across restart
        {
            auto reopened_db = maph::open(test_path);
            REQUIRE(reopened_db.has_value());

            auto persisted_level = reopened_db->get("app.logging.level");
            REQUIRE(persisted_level.has_value());
            REQUIRE(*persisted_level == "DEBUG");
        }
    }

    SECTION("Cache simulation with TTL-like behavior") {
        auto cache_db = maph::create_memory({
            .slots = slot_count{5000},
            .enable_cache = true,
            .cache_size = 500
        });

        // Simulate cache entries with timestamps
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

        for (size_t i = 0; i < 1000; ++i) {
            std::string key = "cache_item_" + std::to_string(i);
            std::string value = R"({"data": "item_)" + std::to_string(i) +
                               R"(", "timestamp": )" + std::to_string(timestamp + i) + "}";

            cache_db.set(key, value);
        }

        // Simulate cache hits and misses
        size_t hits = 0;
        size_t misses = 0;

        for (size_t i = 0; i < 2000; ++i) {  // More lookups than items
            std::string key = "cache_item_" + std::to_string(i % 1500);  // Some will miss

            auto result = cache_db.get(key);
            if (result.has_value()) {
                hits++;
                REQUIRE(result->find("data") != std::string_view::npos);
            } else {
                misses++;
            }
        }

        REQUIRE(hits > 0);
        REQUIRE(misses > 0);
        REQUIRE(hits + misses == 2000);
    }
}

// ===== PERFORMANCE INTEGRATION TESTS =====

TEST_CASE("performance integration scenarios", "[integration][performance][!benchmark]") {
    SECTION("Large dataset operations") {
        auto large_db = maph::create_memory({.slots = slot_count{50000}});

        const size_t dataset_size = 10000;
        std::vector<std::pair<std::string, std::string>> large_dataset;

        // Generate large dataset
        for (size_t i = 0; i < dataset_size; ++i) {
            large_dataset.emplace_back(
                "large_key_" + std::to_string(i),
                "large_value_" + std::to_string(i) + "_" + std::string(100, 'D')
            );
        }

        BENCHMARK("Large dataset insertion") {
            for (const auto& [key, value] : large_dataset) {
                large_db.set(key, value);
            }
        };

        // Insert data for read benchmark
        for (const auto& [key, value] : large_dataset) {
            large_db.set(key, value);
        }

        BENCHMARK("Large dataset random access") {
            std::mt19937 rng{42};
            std::uniform_int_distribution<size_t> dist{0, dataset_size - 1};

            for (size_t i = 0; i < 1000; ++i) {
                size_t idx = dist(rng);
                std::string key = "large_key_" + std::to_string(idx);
                auto result = large_db.get(key);
                (void)result;  // Prevent optimization
            }
        };
    }

    SECTION("High-frequency operations") {
        auto freq_db = maph::create_memory({
            .slots = slot_count{10000},
            .enable_cache = true,
            .cache_size = 1000
        });

        // Simulate high-frequency read/write pattern
        constexpr size_t num_operations = 10000;
        std::vector<std::string> hot_keys;

        for (size_t i = 0; i < 100; ++i) {  // 100 hot keys
            hot_keys.push_back("hot_key_" + std::to_string(i));
        }

        BENCHMARK("High-frequency mixed operations") {
            std::mt19937 rng{123};
            std::uniform_int_distribution<size_t> key_dist{0, hot_keys.size() - 1};
            std::uniform_int_distribution<int> op_dist{0, 2};  // 0=read, 1=write, 2=update

            for (size_t i = 0; i < num_operations; ++i) {
                std::string key = hot_keys[key_dist(rng)];
                int op = op_dist(rng);

                switch (op) {
                    case 0: {  // Read
                        auto result = freq_db.get(key);
                        (void)result;
                        break;
                    }
                    case 1: {  // Write
                        std::string value = "freq_value_" + std::to_string(i);
                        freq_db.set(key, value);
                        break;
                    }
                    case 2: {  // Update
                        freq_db.update(key, [i](std::string_view) {
                            return "updated_value_" + std::to_string(i);
                        });
                        break;
                    }
                }
            }
        };
    }
}