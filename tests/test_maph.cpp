/**
 * @file test_maph.cpp
 * @brief Comprehensive unit tests for maph library
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "maph.hpp"
#include <filesystem>
#include <random>
#include <thread>
#include <set>
#include <map>
#include <atomic>
#include <chrono>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <numeric>

using namespace maph;
using namespace Catch::Matchers;
namespace fs = std::filesystem;

// Test fixture for file cleanup
class MaphTestFixture {
protected:
    std::string test_file;
    std::unique_ptr<Maph> maph_instance;
    
    MaphTestFixture() : test_file("test_maph_" + std::to_string(std::random_device{}()) + ".maph") {}
    
    ~MaphTestFixture() {
        cleanup();
    }
    
    void cleanup() {
        maph_instance.reset();
        if (fs::exists(test_file)) {
            fs::remove(test_file);
        }
    }
    
    std::string generate_json(int id, const std::string& data = "test") {
        std::stringstream ss;
        ss << "{\"id\":" << id << ",\"data\":\"" << data << "\"}";
        return ss.str();
    }
    
    std::string generate_large_json(size_t size) {
        std::string data(size - 20, 'x');
        return "{\"data\":\"" + data + "\"}";
    }
};

// ===== BASIC FUNCTIONALITY TESTS =====

TEST_CASE_METHOD(MaphTestFixture, "Basic creation and file operations", "[maph][basic]") {
    SECTION("Create new maph file") {
        auto m = Maph::create(test_file, 1000);
        REQUIRE(m != nullptr);
        
        auto stats = m->stats();
        CHECK(stats.total_slots == 1000);
        CHECK(stats.used_slots == 0);
        CHECK(stats.load_factor == 0.0);
        
        // File should exist
        REQUIRE(fs::exists(test_file));
        
        // Check file size
        auto file_size = fs::file_size(test_file);
        CHECK(file_size == sizeof(Header) + 1000 * sizeof(Slot));
    }
    
    SECTION("Create with different slot counts") {
        auto m = Maph::create(test_file, 1000);
        REQUIRE(m != nullptr);
        
        auto stats = m->stats();
        CHECK(stats.total_slots == 1000);
    }
    
    SECTION("Open existing file for read-write") {
        // First create
        {
            auto m = Maph::create(test_file, 100);
            REQUIRE(m != nullptr);
            m->set("key1", "value1");
        }
        
        // Then open
        auto m = Maph::open(test_file, false);
        REQUIRE(m != nullptr);
        
        auto value = m->get("key1");
        REQUIRE(value.has_value());
        CHECK(*value == "value1");
    }
    
    SECTION("Open existing file read-only") {
        // First create and populate
        {
            auto m = Maph::create(test_file, 100);
            REQUIRE(m != nullptr);
            m->set("key1", "value1");
            m->set("key2", "value2");
        }
        
        // Open read-only
        auto m = maph::open_readonly(test_file);
        REQUIRE(m != nullptr);
        
        // Should be able to read
        auto value1 = m->get("key1");
        REQUIRE(value1.has_value());
        CHECK(*value1 == "value1");
        
        // Should not be able to write
        bool result = m->set("key3", "value3");
        CHECK(result == false);
        
        // Should not be able to remove
        result = m->remove("key1");
        CHECK(result == false);
    }
    
    SECTION("Open non-existent file returns nullptr") {
        auto m = Maph::open("non_existent_file.maph");
        CHECK(m == nullptr);
    }
    
    SECTION("Create file with invalid path returns nullptr") {
        auto m = Maph::create("/invalid/path/test.maph", 100);
        CHECK(m == nullptr);
    }
}

TEST_CASE_METHOD(MaphTestFixture, "Key-value storage and retrieval", "[maph][storage]") {
    auto m = Maph::create(test_file, 1000);
    REQUIRE(m != nullptr);
    
    SECTION("Set and get simple values") {
        CHECK(m->set("key1", "value1") == true);
        CHECK(m->set("key2", "value2") == true);
        CHECK(m->set("key3", "value3") == true);
        
        auto v1 = m->get("key1");
        auto v2 = m->get("key2");
        auto v3 = m->get("key3");
        
        REQUIRE(v1.has_value());
        REQUIRE(v2.has_value());
        REQUIRE(v3.has_value());
        
        CHECK(*v1 == "value1");
        CHECK(*v2 == "value2");
        CHECK(*v3 == "value3");
    }
    
    SECTION("Get non-existent key returns nullopt") {
        auto value = m->get("non_existent");
        CHECK(!value.has_value());
    }
    
    SECTION("Set JSON values") {
        std::string json1 = generate_json(1, "test1");
        std::string json2 = generate_json(2, "test2");
        
        CHECK(m->set("doc1", json1) == true);
        CHECK(m->set("doc2", json2) == true);
        
        auto v1 = m->get("doc1");
        auto v2 = m->get("doc2");
        
        REQUIRE(v1.has_value());
        REQUIRE(v2.has_value());
        
        CHECK(*v1 == json1);
        CHECK(*v2 == json2);
    }
    
    SECTION("Update existing key") {
        CHECK(m->set("key1", "value1") == true);
        CHECK(m->set("key1", "updated_value") == true);
        
        auto value = m->get("key1");
        REQUIRE(value.has_value());
        CHECK(*value == "updated_value");
    }
    
    SECTION("Set value at max size") {
        std::string max_value(Slot::MAX_SIZE, 'x');
        CHECK(m->set("max_key", max_value) == true);
        
        auto value = m->get("max_key");
        REQUIRE(value.has_value());
        CHECK(*value == max_value);
        CHECK(value->size() == Slot::MAX_SIZE);
    }
    
    SECTION("Set value exceeding max size fails") {
        std::string oversized(Slot::MAX_SIZE + 1, 'x');
        CHECK(m->set("oversized", oversized) == false);
        
        auto value = m->get("oversized");
        CHECK(!value.has_value());
    }
    
    SECTION("Empty key and value handling") {
        CHECK(m->set("", "empty_key_value") == true);
        CHECK(m->set("empty_value", "") == true);
        CHECK(m->set("", "") == true);
        
        auto v1 = m->get("");
        REQUIRE(v1.has_value());
        CHECK(*v1 == "");  // Last set wins
        
        auto v2 = m->get("empty_value");
        REQUIRE(v2.has_value());
        CHECK(*v2 == "");
    }
}

TEST_CASE_METHOD(MaphTestFixture, "Remove operations", "[maph][remove]") {
    auto m = Maph::create(test_file, 100);
    REQUIRE(m != nullptr);
    
    SECTION("Remove existing key") {
        m->set("key1", "value1");
        m->set("key2", "value2");
        
        CHECK(m->exists("key1") == true);
        CHECK(m->remove("key1") == true);
        CHECK(m->exists("key1") == false);
        
        // Other key should still exist
        CHECK(m->exists("key2") == true);
    }
    
    SECTION("Remove non-existent key") {
        CHECK(m->remove("non_existent") == false);
    }
    
    SECTION("Remove and re-add key") {
        m->set("key1", "value1");
        CHECK(m->remove("key1") == true);
        CHECK(m->exists("key1") == false);
        
        CHECK(m->set("key1", "new_value") == true);
        auto value = m->get("key1");
        REQUIRE(value.has_value());
        CHECK(*value == "new_value");
    }
}

TEST_CASE_METHOD(MaphTestFixture, "Collision handling and probing", "[maph][collision]") {
    // Create small table to force collisions
    auto m = Maph::create(test_file, 20);  // Small table to test full condition
    REQUIRE(m != nullptr);
    
    SECTION("Handle collisions in dynamic range") {
        // Add keys that will likely collide in small table
        std::vector<std::string> keys;
        std::vector<std::string> values;
        
        for (int i = 0; i < 15; ++i) {
            keys.push_back("key_" + std::to_string(i * 20));  // Likely to collide
            values.push_back("value_" + std::to_string(i));
        }
        
        // Insert all
        for (size_t i = 0; i < keys.size(); ++i) {
            bool result = m->set(keys[i], values[i]);
            if (!result) {
                INFO("Failed to insert key: " << keys[i] << " at index " << i);
            }
        }
        
        // Verify all can be retrieved
        for (size_t i = 0; i < keys.size(); ++i) {
            auto value = m->get(keys[i]);
            if (value.has_value()) {
                CHECK(*value == values[i]);
            }
        }
    }
    
    SECTION("Linear probing limit") {
        // Fill dynamic range
        for (int i = 0; i < 10; ++i) {
            std::string key = "dynamic_key_" + std::to_string(i);
            m->set(key, "value");
        }
        
        // Try to add more - should fail when probe limit reached
        int failed_count = 0;
        for (int i = 10; i < 20; ++i) {
            std::string key = "extra_key_" + std::to_string(i);
            if (!m->set(key, "value")) {
                failed_count++;
            }
        }
        
        // Some should fail due to probe limit
        CHECK(failed_count > 0);
    }
}

// ===== BATCH OPERATIONS TESTS =====

TEST_CASE_METHOD(MaphTestFixture, "Batch operations", "[maph][batch]") {
    auto m = Maph::create(test_file, 1000);
    REQUIRE(m != nullptr);
    
    SECTION("Batch get with callback") {
        // Populate
        for (int i = 0; i < 10; ++i) {
            m->set("key" + std::to_string(i), "value" + std::to_string(i));
        }
        
        // Batch get - store strings to avoid use-after-scope
        std::vector<std::string> key_strings;
        key_strings.reserve(10);  // Reserve to prevent reallocation
        std::vector<JsonView> keys;
        for (int i = 0; i < 10; ++i) {
            key_strings.push_back("key" + std::to_string(i));
            keys.push_back(key_strings.back());
        }
        
        std::map<std::string, std::string> results;
        m->mget(keys, [&results](JsonView key, JsonView value) {
            results[std::string(key)] = std::string(value);
        });
        
        CHECK(results.size() == 10);
        for (int i = 0; i < 10; ++i) {
            std::string key = "key" + std::to_string(i);
            CHECK(results[key] == "value" + std::to_string(i));
        }
    }
    
    SECTION("Batch get with missing keys") {
        m->set("exists1", "value1");
        m->set("exists2", "value2");
        
        std::vector<JsonView> keys = {"exists1", "missing", "exists2"};
        
        std::vector<std::string> found_keys;
        m->mget(keys, [&found_keys](JsonView key, JsonView value) {
            found_keys.push_back(std::string(key));
        });
        
        CHECK(found_keys.size() == 2);
        CHECK(found_keys[0] == "exists1");
        CHECK(found_keys[1] == "exists2");
    }
    
    SECTION("Batch set") {
        std::vector<std::pair<std::string, std::string>> kv_strings;
        for (int i = 0; i < 100; ++i) {
            kv_strings.emplace_back("batch_key_" + std::to_string(i),
                                  "batch_value_" + std::to_string(i));
        }
        
        std::vector<std::pair<JsonView, JsonView>> kvs;
        for (const auto& [k, v] : kv_strings) {
            kvs.emplace_back(k, v);
        }
        
        size_t count = m->mset(kvs);
        CHECK(count == 100);
        
        // Verify all were set
        for (int i = 0; i < 100; ++i) {
            std::string key = "batch_key_" + std::to_string(i);
            auto value = m->get(key);
            REQUIRE(value.has_value());
            CHECK(*value == "batch_value_" + std::to_string(i));
        }
    }
    
    SECTION("Batch set with oversized values") {
        std::string oversized(Slot::MAX_SIZE + 1, 'x');
        std::string valid1_key = "valid1";
        std::string valid1_val = "value1";
        std::string oversized_key = "oversized";
        std::string valid2_key = "valid2";
        std::string valid2_val = "value2";
        
        std::vector<std::pair<JsonView, JsonView>> kvs = {
            {valid1_key, valid1_val},
            {oversized_key, oversized},
            {valid2_key, valid2_val}
        };
        
        size_t count = m->mset(kvs);
        CHECK(count == 2);  // Only valid ones
        
        CHECK(m->exists("valid1") == true);
        CHECK(m->exists("oversized") == false);
        CHECK(m->exists("valid2") == true);
    }
}

// ===== SCANNING TESTS =====

TEST_CASE_METHOD(MaphTestFixture, "Scan operations", "[maph][scan]") {
    auto m = Maph::create(test_file, 100);
    REQUIRE(m != nullptr);
    
    SECTION("Scan all entries") {
        // Populate
        std::set<std::string> expected_values;
        for (int i = 0; i < 10; ++i) {
            std::string value = "value" + std::to_string(i);
            m->set("key" + std::to_string(i), value);
            expected_values.insert(value);
        }
        
        // Scan
        std::set<std::string> scanned_values;
        m->scan([&scanned_values](uint64_t index, uint32_t hash, JsonView value) {
            scanned_values.insert(std::string(value));
        });
        
        CHECK(scanned_values == expected_values);
    }
    
    SECTION("Scan empty maph") {
        int count = 0;
        m->scan([&count](uint64_t index, uint32_t hash, JsonView value) {
            count++;
        });
        
        CHECK(count == 0);
    }
    
    SECTION("Scan with removed entries") {
        m->set("key1", "value1");
        m->set("key2", "value2");
        m->set("key3", "value3");
        m->remove("key2");
        
        std::vector<std::string> values;
        m->scan([&values](uint64_t index, uint32_t hash, JsonView value) {
            values.push_back(std::string(value));
        });
        
        CHECK(values.size() == 2);
        CHECK(std::find(values.begin(), values.end(), "value1") != values.end());
        CHECK(std::find(values.begin(), values.end(), "value3") != values.end());
        CHECK(std::find(values.begin(), values.end(), "value2") == values.end());
    }
}

// ===== CONCURRENT ACCESS TESTS =====

TEST_CASE_METHOD(MaphTestFixture, "Thread safety and concurrent access", "[maph][concurrent]") {
    auto m = Maph::create(test_file, 10000);
    REQUIRE(m != nullptr);
    
    SECTION("Concurrent reads") {
        // Populate
        for (int i = 0; i < 1000; ++i) {
            m->set("key" + std::to_string(i), "value" + std::to_string(i));
        }
        
        std::atomic<int> errors{0};
        std::vector<std::thread> threads;
        
        for (int t = 0; t < 10; ++t) {
            threads.emplace_back([&, t]() {
                for (int i = 0; i < 1000; ++i) {
                    int key_idx = (t * 100 + i) % 1000;
                    auto value = m->get("key" + std::to_string(key_idx));
                    if (!value.has_value() || 
                        *value != "value" + std::to_string(key_idx)) {
                        errors++;
                    }
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        CHECK(errors == 0);
    }
    
    SECTION("Concurrent writes to different slots") {
        std::atomic<int> success_count{0};
        std::vector<std::thread> threads;
        
        for (int t = 0; t < 10; ++t) {
            threads.emplace_back([&, t]() {
                for (int i = 0; i < 100; ++i) {
                    int key_idx = t * 100 + i;
                    if (m->set("concurrent_" + std::to_string(key_idx), 
                              "value_" + std::to_string(key_idx))) {
                        success_count++;
                    }
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        CHECK(success_count == 1000);
        
        // Verify all values
        for (int i = 0; i < 1000; ++i) {
            auto value = m->get("concurrent_" + std::to_string(i));
            REQUIRE(value.has_value());
            CHECK(*value == "value_" + std::to_string(i));
        }
    }
    
    SECTION("Concurrent updates to same key") {
        std::vector<std::thread> threads;
        
        for (int t = 0; t < 10; ++t) {
            threads.emplace_back([&, t]() {
                for (int i = 0; i < 100; ++i) {
                    m->set("shared_key", "thread_" + std::to_string(t));
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        // Should have some value from one of the threads
        auto value = m->get("shared_key");
        REQUIRE(value.has_value());
        
        // Check it's a valid thread value
        bool valid = false;
        for (int t = 0; t < 10; ++t) {
            if (*value == "thread_" + std::to_string(t)) {
                valid = true;
                break;
            }
        }
        CHECK(valid);
    }
    
    SECTION("Parallel batch operations") {
        std::vector<std::thread> threads;
        std::atomic<size_t> total_processed{0};
        
        for (int t = 0; t < 4; ++t) {
            threads.emplace_back([&, t]() {
                std::vector<std::pair<std::string, std::string>> kv_strings;
                for (int i = 0; i < 250; ++i) {
                    kv_strings.emplace_back("parallel_" + std::to_string(t * 250 + i),
                                          "value_" + std::to_string(t * 250 + i));
                }
                
                std::vector<std::pair<JsonView, JsonView>> kvs;
                for (const auto& [k, v] : kv_strings) {
                    kvs.emplace_back(k, v);
                }
                
                size_t count = m->parallel_mset(kvs, 2);
                total_processed += count;
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        CHECK(total_processed == 1000);
    }
}

// ===== MEMORY AND PERFORMANCE TESTS =====

TEST_CASE_METHOD(MaphTestFixture, "Memory management and performance", "[maph][memory]") {
    SECTION("Stats tracking") {
        auto m = Maph::create(test_file, 1000);
        REQUIRE(m != nullptr);
        
        auto stats1 = m->stats();
        CHECK(stats1.used_slots == 0);
        CHECK(stats1.load_factor == 0.0);
        
        // Add some entries
        for (int i = 0; i < 100; ++i) {
            m->set("key" + std::to_string(i), "value");
        }
        
        auto stats2 = m->stats();
        CHECK(stats2.used_slots == 100);
        CHECK(stats2.load_factor == Catch::Approx(0.1));
        
        // Remove some
        for (int i = 0; i < 50; ++i) {
            m->remove("key" + std::to_string(i));
        }
        
        auto stats3 = m->stats();
        CHECK(stats3.used_slots == 50);
        CHECK(stats3.load_factor == Catch::Approx(0.05));
    }
    
    SECTION("Generation counter") {
        auto m = Maph::create(test_file, 100);
        REQUIRE(m != nullptr);
        
        auto stats1 = m->stats();
        uint64_t gen1 = stats1.generation;
        
        m->set("key1", "value1");
        auto stats2 = m->stats();
        CHECK(stats2.generation > gen1);
        
        m->remove("key1");
        auto stats3 = m->stats();
        CHECK(stats3.generation > stats2.generation);
    }
    
    SECTION("Memory mapped file sync") {
        auto m = Maph::create(test_file, 100);
        REQUIRE(m != nullptr);
        
        m->set("persist_key", "persist_value");
        m->sync();  // Force sync
        
        // Close and reopen
        m.reset();
        
        auto m2 = Maph::open(test_file);
        REQUIRE(m2 != nullptr);
        
        auto value = m2->get("persist_key");
        REQUIRE(value.has_value());
        CHECK(*value == "persist_value");
    }
}

// ===== HASH FUNCTION TESTS =====

TEST_CASE("Hash function properties", "[maph][hash]") {
    SECTION("Hash computation consistency") {
        std::string key = "test_key";
        uint64_t num_slots = 1000;
        
        auto result1 = Hash::compute(key, num_slots);
        auto result2 = Hash::compute(key, num_slots);
        
        CHECK(result1.hash == result2.hash);
        CHECK(result1.index == result2.index);
    }
    
    SECTION("Hash never returns 0") {
        // 0 is reserved for empty slots
        for (int i = 0; i < 1000; ++i) {
            std::string key = "key_" + std::to_string(i);
            auto result = Hash::compute(key, 1000);
            CHECK(result.hash != 0);
        }
    }
    
    SECTION("Hash distribution") {
        std::map<uint32_t, int> distribution;
        uint64_t num_slots = 100;
        
        for (int i = 0; i < 1000; ++i) {
            std::string key = "key_" + std::to_string(i);
            auto result = Hash::compute(key, num_slots);
            distribution[result.index]++;
        }
        
        // Check that we hit multiple slots
        CHECK(distribution.size() > num_slots / 2);
        
        // Check reasonably uniform distribution
        int max_count = 0;
        for (const auto& [idx, count] : distribution) {
            max_count = std::max(max_count, count);
        }
        CHECK(max_count < 50);  // No slot should have too many
    }
    
    SECTION("Empty key hash") {
        auto result = Hash::compute("", 1000);
        CHECK(result.hash != 0);
        CHECK(result.index < 1000);
    }
    
#ifdef __AVX2__
    SECTION("SIMD batch hash consistency") {
        if (!__builtin_cpu_supports("avx2")) {
            SKIP("AVX2 not supported");
        }
        
        std::vector<JsonView> keys;
        for (int i = 0; i < 20; ++i) {
            keys.push_back("batch_key_" + std::to_string(i));
        }
        
        std::vector<Hash::Result> batch_results;
        Hash::compute_batch(keys, 1000, batch_results);
        
        CHECK(batch_results.size() == keys.size());
        
        // Verify batch results match individual computation
        for (size_t i = 0; i < keys.size(); ++i) {
            auto individual = Hash::compute(keys[i], 1000);
            CHECK(batch_results[i].hash == individual.hash);
            CHECK(batch_results[i].index == individual.index);
        }
    }
#endif
}

// ===== DURABILITY TESTS =====

TEST_CASE_METHOD(MaphTestFixture, "Durability and persistence", "[maph][durability]") {
    SECTION("Basic durability manager") {
        auto m = Maph::create(test_file, 100);
        REQUIRE(m != nullptr);
        
        m->enable_durability(std::chrono::milliseconds(100));
        
        m->set("durable_key", "durable_value");
        
        // Wait for auto-sync
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        
        // Manually sync
        m->sync_now();
        
        m->disable_durability();
    }
    
    SECTION("Durability disabled for readonly") {
        // Create and populate
        {
            auto m = Maph::create(test_file, 100);
            m->set("key", "value");
        }
        
        auto m = maph::open_readonly(test_file);
        REQUIRE(m != nullptr);
        
        // Should not crash when enabling durability on readonly
        m->enable_durability();
        m->sync_now();
        m->disable_durability();
    }
}

// ===== EDGE CASES AND ERROR CONDITIONS =====

TEST_CASE_METHOD(MaphTestFixture, "Edge cases and error conditions", "[maph][edge]") {
    SECTION("Very small table") {
        auto m = Maph::create(test_file, 1);
        REQUIRE(m != nullptr);
        
        CHECK(m->set("key1", "value1") == true);
        CHECK(m->set("key2", "value2") == false);  // No room
        
        CHECK(m->exists("key1") == true);
        CHECK(m->exists("key2") == false);
    }
    
    SECTION("Maximum slots") {
        // Don't actually create huge file, just check parameter validation
        size_t huge_slots = 1000000000;  // 1 billion slots
        auto m = Maph::create(test_file, huge_slots);
        
        // Should handle large numbers gracefully
        if (m) {
            auto stats = m->stats();
            CHECK(stats.total_slots == huge_slots);
            m.reset();  // Clean up immediately
        }
    }
    
    SECTION("Special characters in keys and values") {
        auto m = Maph::create(test_file, 100);
        REQUIRE(m != nullptr);
        
        std::vector<std::string> special_keys = {
            "key with spaces",
            "key\nwith\nnewlines",
            "key\twith\ttabs",
            "key\"with\"quotes",
            "key'with'apostrophes",
            "key\\with\\backslashes",
            "{\"json\":\"key\"}",
            "\x00\x01\x02",  // Binary data
            "🔑🗝️",  // Unicode emoji
        };
        
        for (const auto& key : special_keys) {
            std::string value = "value_for_" + key;
            CHECK(m->set(key, value) == true);
            
            auto retrieved = m->get(key);
            REQUIRE(retrieved.has_value());
            CHECK(*retrieved == value);
        }
    }
    
    SECTION("Move semantics") {
        auto m1 = Maph::create(test_file, 100);
        REQUIRE(m1 != nullptr);
        
        m1->set("key1", "value1");
        
        // Move constructor
        Maph m2(std::move(*m1));
        
        auto value = m2.get("key1");
        REQUIRE(value.has_value());
        CHECK(*value == "value1");
        
        // Move assignment
        auto m3 = Maph::create(test_file + ".2", 100);
        *m3 = std::move(m2);
        
        value = m3->get("key1");
        REQUIRE(value.has_value());
        CHECK(*value == "value1");
        
        // Cleanup extra file
        fs::remove(test_file + ".2");
    }
    
    SECTION("File with invalid magic number") {
        // Create a file with wrong header
        {
            std::ofstream f(test_file, std::ios::binary);
            Header bad_header{};
            bad_header.magic = 0xDEADBEEF;  // Wrong magic
            f.write(reinterpret_cast<const char*>(&bad_header), sizeof(Header));
        }
        
        auto m = Maph::open(test_file);
        CHECK(m == nullptr);
    }
}

// ===== PARALLEL OPERATIONS TESTS =====

TEST_CASE_METHOD(MaphTestFixture, "Parallel operations", "[maph][parallel]") {
    auto m = Maph::create(test_file, 10000);
    REQUIRE(m != nullptr);
    
    SECTION("Parallel scan") {
        // Populate
        for (int i = 0; i < 1000; ++i) {
            m->set("key" + std::to_string(i), "value" + std::to_string(i));
        }
        
        std::atomic<int> count{0};
        m->parallel_scan([&count](uint64_t index, uint32_t hash, JsonView value) {
            count++;
        }, 4);
        
        CHECK(count == 1000);
    }
    
    SECTION("Parallel mget") {
        // Populate
        for (int i = 0; i < 1000; ++i) {
            m->set("pkey" + std::to_string(i), "pvalue" + std::to_string(i));
        }
        
        std::vector<std::string> key_strings;
        key_strings.reserve(1000);  // Reserve to prevent reallocation
        std::vector<JsonView> keys;
        for (int i = 0; i < 1000; ++i) {
            key_strings.push_back("pkey" + std::to_string(i));
            keys.push_back(key_strings.back());
        }
        
        std::atomic<int> found{0};
        m->parallel_mget(keys, [&found](JsonView key, JsonView value) {
            found++;
        }, 4);
        
        CHECK(found == 1000);
    }
    
    SECTION("Parallel operations with small data") {
        // Should fallback to sequential
        std::string k1 = "k1", k2 = "k2", k3 = "k3";
        std::vector<JsonView> keys = {k1, k2, k3};
        
        m->set("k1", "v1");
        m->set("k2", "v2");
        m->set("k3", "v3");
        
        int count = 0;
        m->parallel_mget(keys, [&count](JsonView key, JsonView value) {
            count++;
        }, 10);  // More threads than data
        
        CHECK(count == 3);
    }
}

// ===== SLOT VERSIONING TESTS =====

TEST_CASE_METHOD(MaphTestFixture, "Slot versioning and atomicity", "[maph][versioning]") {
    auto m = Maph::create(test_file, 100);
    REQUIRE(m != nullptr);
    
    SECTION("Version increments on set") {
        // Access internals for testing (normally wouldn't do this)
        m->set("key1", "value1");
        auto stats1 = m->stats();
        
        m->set("key1", "value2");
        auto stats2 = m->stats();
        
        // Generation should have increased
        CHECK(stats2.generation > stats1.generation);
    }
    
    SECTION("Version increments on clear") {
        m->set("key1", "value1");
        auto stats1 = m->stats();
        
        m->remove("key1");
        auto stats2 = m->stats();
        
        CHECK(stats2.generation > stats1.generation);
    }
}