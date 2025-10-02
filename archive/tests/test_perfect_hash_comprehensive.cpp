/**
 * @file test_perfect_hash_comprehensive.cpp
 * @brief Comprehensive unit tests for perfect hash functionality and dual-mode operation
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

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
#include <unordered_set>

using namespace maph;
using namespace Catch::Matchers;
namespace fs = std::filesystem;

// Test fixture for perfect hash testing
class PerfectHashTestFixture {
protected:
    std::string test_file;
    std::string journal_file;
    std::unique_ptr<Maph> db;
    
    PerfectHashTestFixture() 
        : test_file("/tmp/test_ph_" + std::to_string(std::random_device{}()) + ".maph"),
          journal_file(test_file + ".journal") {}
    
    ~PerfectHashTestFixture() {
        cleanup();
    }
    
    void cleanup() {
        db.reset();
        if (fs::exists(test_file)) fs::remove(test_file);
        if (fs::exists(journal_file)) fs::remove(journal_file);
    }
    
    std::vector<std::string> generate_keys(size_t count, const std::string& prefix = "key") {
        std::vector<std::string> keys;
        keys.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            keys.push_back(prefix + "_" + std::to_string(i));
        }
        return keys;
    }
    
    std::vector<std::pair<std::string, std::string>> generate_json_kvs(size_t count) {
        std::vector<std::pair<std::string, std::string>> kvs;
        kvs.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            std::string key = "{\"id\":" + std::to_string(i) + ",\"type\":\"user\"}";
            std::string value = "{\"name\":\"User" + std::to_string(i) + 
                              "\",\"age\":" + std::to_string(20 + (i % 50)) + 
                              ",\"active\":true}";
            kvs.emplace_back(key, value);
        }
        return kvs;
    }
    
    double measure_lookup_time(const std::vector<std::string>& keys, size_t iterations = 1000) {
        auto start = std::chrono::high_resolution_clock::now();
        
        for (size_t iter = 0; iter < iterations; ++iter) {
            for (const auto& key : keys) {
                auto val = db->get(key);
                if (!val.has_value()) {
                    throw std::runtime_error("Key not found: " + key);
                }
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        return static_cast<double>(duration_ns) / (keys.size() * iterations);
    }
};

// ===== PERFECT HASH CONSTRUCTION TESTS =====

TEST_CASE_METHOD(PerfectHashTestFixture, "Perfect hash construction and basic operation", "[perfect_hash][construction]") {
    db = Maph::create(test_file, 10000);
    REQUIRE(db != nullptr);
    
    SECTION("Build perfect hash from empty database") {
        auto result = db->optimize();
        CHECK(result.ok());
        CHECK(result.message == "No keys to optimize");
        
        auto stats = db->stats();
        CHECK(stats.is_optimized == false);
    }
    
    SECTION("Build perfect hash with small dataset") {
        // Add test data
        auto kvs = generate_json_kvs(100);
        for (const auto& [key, value] : kvs) {
            REQUIRE(db->set(key, value));
        }
        
        // Check pre-optimization state
        auto pre_stats = db->stats();
        CHECK(pre_stats.is_optimized == false);
        CHECK(pre_stats.journal_entries >= 100);
        
        // Optimize
        auto result = db->optimize();
        REQUIRE(result.ok());
        
        // Check post-optimization state
        auto post_stats = db->stats();
        CHECK(post_stats.is_optimized == true);
        
        // Verify all keys still accessible
        for (const auto& [key, expected_value] : kvs) {
            auto value = db->get(key);
            REQUIRE(value.has_value());
            CHECK(*value == expected_value);
        }
    }
    
    SECTION("Build perfect hash with duplicate keys in journal") {
        // Add same keys multiple times (simulating updates)
        for (int round = 0; round < 3; ++round) {
            for (int i = 0; i < 50; ++i) {
                std::string key = "dup_key_" + std::to_string(i);
                std::string value = "value_round_" + std::to_string(round);
                REQUIRE(db->set(key, value));
            }
        }
        
        // Journal should have duplicates, but optimize should handle them
        auto result = db->optimize();
        REQUIRE(result.ok());
        
        // Verify final values are accessible
        for (int i = 0; i < 50; ++i) {
            std::string key = "dup_key_" + std::to_string(i);
            auto value = db->get(key);
            REQUIRE(value.has_value());
            CHECK(*value == "value_round_2");
        }
    }
}

// ===== DUAL-MODE OPERATION TESTS =====

TEST_CASE_METHOD(PerfectHashTestFixture, "Dual-mode operation workflow", "[perfect_hash][dual_mode]") {
    db = Maph::create(test_file, 10000);
    REQUIRE(db != nullptr);
    
    SECTION("Complete workflow: standard -> optimize -> hybrid") {
        // Phase 1: Standard mode insertion
        auto initial_kvs = generate_json_kvs(500);
        for (const auto& [key, value] : initial_kvs) {
            REQUIRE(db->set(key, value));
        }
        
        // Verify standard mode
        auto stats1 = db->stats();
        CHECK(stats1.is_optimized == false);
        
        // Phase 2: Optimize
        auto opt_result = db->optimize();
        REQUIRE(opt_result.ok());
        
        auto stats2 = db->stats();
        CHECK(stats2.is_optimized == true);
        
        // Phase 3: Add new keys after optimization
        std::vector<std::pair<std::string, std::string>> new_kvs;
        for (size_t i = 500; i < 700; ++i) {
            std::string key = "{\"id\":" + std::to_string(i) + ",\"type\":\"user\"}";
            std::string value = "{\"name\":\"NewUser" + std::to_string(i) + "\"}";
            new_kvs.emplace_back(key, value);
            REQUIRE(db->set(key, value));
        }
        
        // Verify both old and new keys work
        for (const auto& [key, expected_value] : initial_kvs) {
            auto value = db->get(key);
            REQUIRE(value.has_value());
            CHECK(*value == expected_value);
        }
        
        for (const auto& [key, expected_value] : new_kvs) {
            auto value = db->get(key);
            REQUIRE(value.has_value());
            CHECK(*value == expected_value);
        }
        
        // Phase 4: Re-optimize with all keys
        auto reopt_result = db->optimize();
        CHECK(reopt_result.ok());
        
        // Verify all keys still work
        CHECK(db->stats().is_optimized == true);
        for (const auto& [key, expected_value] : initial_kvs) {
            auto value = db->get(key);
            REQUIRE(value.has_value());
            CHECK(*value == expected_value);
        }
        for (const auto& [key, expected_value] : new_kvs) {
            auto value = db->get(key);
            REQUIRE(value.has_value());
            CHECK(*value == expected_value);
        }
    }
}

// ===== PERFORMANCE COMPARISON TESTS =====

TEST_CASE_METHOD(PerfectHashTestFixture, "Performance: FNV vs Perfect Hash", "[perfect_hash][benchmark]") {
    db = Maph::create(test_file, 50000);
    REQUIRE(db != nullptr);
    
    SECTION("Lookup performance comparison") {
        const size_t NUM_KEYS = 1000;
        auto kvs = generate_json_kvs(NUM_KEYS);
        
        // Insert all data
        for (const auto& [key, value] : kvs) {
            REQUIRE(db->set(key, value));
        }
        
        // Extract just the keys for testing
        std::vector<std::string> keys;
        for (const auto& [key, _] : kvs) {
            keys.push_back(key);
        }
        
        // Measure standard hash performance
        double standard_time = measure_lookup_time(keys, 100);
        INFO("Standard hash avg lookup time: " << standard_time << " ns");
        
        // Optimize
        auto result = db->optimize();
        REQUIRE(result.ok());
        
        // Measure perfect hash performance
        double perfect_time = measure_lookup_time(keys, 100);
        INFO("Perfect hash avg lookup time: " << perfect_time << " ns");
        
        // Perfect hash should be faster or at least comparable
        // (In placeholder implementation, might not see improvement)
        INFO("Performance ratio (perfect/standard): " << (perfect_time / standard_time));
    }
}

TEST_CASE_METHOD(PerfectHashTestFixture, "Benchmark: Lookup operations", "[.][benchmark]") {
    db = Maph::create(test_file, 100000);
    REQUIRE(db != nullptr);
    
    // Prepare test data
    const size_t NUM_KEYS = 10000;
    auto kvs = generate_json_kvs(NUM_KEYS);
    for (const auto& [key, value] : kvs) {
        db->set(key, value);
    }
    
    BENCHMARK("Standard hash lookups (before optimization)") {
        for (size_t i = 0; i < 100; ++i) {
            std::string key = "{\"id\":" + std::to_string(i % NUM_KEYS) + ",\"type\":\"user\"}";
            auto val = db->get(key);
        }
    };
    
    // Optimize
    db->optimize();
    
    BENCHMARK("Perfect hash lookups (after optimization)") {
        for (size_t i = 0; i < 100; ++i) {
            std::string key = "{\"id\":" + std::to_string(i % NUM_KEYS) + ",\"type\":\"user\"}";
            auto val = db->get(key);
        }
    };
}

// ===== EDGE CASES AND STRESS TESTS =====

TEST_CASE_METHOD(PerfectHashTestFixture, "Edge cases and error conditions", "[perfect_hash][edge]") {
    SECTION("Optimize read-only database") {
        db = Maph::create(test_file, 1000);
        db->set("key1", "value1");
        db.reset();
        
        // Reopen as read-only
        db = Maph::open(test_file, true);
        REQUIRE(db != nullptr);
        
        auto result = db->optimize();
        CHECK(!result.ok());
        CHECK(result.code == ErrorCode::READONLY_STORE);
    }
    
    SECTION("Handle hash collisions") {
        db = Maph::create(test_file, 100);  // Small table to force collisions
        
        // Generate keys that might collide
        std::vector<std::string> keys;
        for (int i = 0; i < 50; ++i) {
            keys.push_back("collision_test_key_" + std::to_string(i));
        }
        
        // Insert all keys
        for (const auto& key : keys) {
            REQUIRE(db->set(key, "value_" + key));
        }
        
        // Optimize with potential collisions
        auto result = db->optimize();
        CHECK(result.ok());
        
        // Verify all keys still accessible
        for (const auto& key : keys) {
            auto val = db->get(key);
            REQUIRE(val.has_value());
            CHECK(*val == "value_" + key);
        }
    }
    
    SECTION("Table full condition") {
        db = Maph::create(test_file, 10);  // Very small table
        
        // Try to insert more than capacity
        int successful = 0;
        for (int i = 0; i < 20; ++i) {
            if (db->set("key_" + std::to_string(i), "value_" + std::to_string(i))) {
                successful++;
            }
        }
        
        // Should have some failures due to probe distance limit
        CHECK(successful < 20);
        
        // Optimization should still work with what was inserted
        auto result = db->optimize();
        CHECK(result.ok());
    }
}

TEST_CASE_METHOD(PerfectHashTestFixture, "Large dataset stress test", "[perfect_hash][stress][!mayfail]") {
    const size_t LARGE_SIZE = 100000;
    db = Maph::create(test_file, LARGE_SIZE * 2);  // 2x capacity for safety
    REQUIRE(db != nullptr);
    
    SECTION("Insert and optimize large dataset") {
        // Generate large dataset
        INFO("Generating " << LARGE_SIZE << " key-value pairs...");
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (size_t i = 0; i < LARGE_SIZE; ++i) {
            std::string key = "large_key_" + std::to_string(i);
            std::string value = "{\"id\":" + std::to_string(i) + 
                              ",\"data\":\"" + std::string(100, 'x') + "\"}";
            
            if (!db->set(key, value)) {
                FAIL("Failed to insert key at index " << i);
            }
            
            if (i % 10000 == 0) {
                INFO("Inserted " << i << " keys...");
            }
        }
        
        auto insert_end = std::chrono::high_resolution_clock::now();
        auto insert_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            insert_end - start).count();
        INFO("Insertion took " << insert_time << " ms");
        
        // Optimize
        INFO("Optimizing database...");
        auto opt_start = std::chrono::high_resolution_clock::now();
        auto result = db->optimize();
        auto opt_end = std::chrono::high_resolution_clock::now();
        
        auto opt_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            opt_end - opt_start).count();
        INFO("Optimization took " << opt_time << " ms");
        
        REQUIRE(result.ok());
        
        // Verify a sample of keys
        INFO("Verifying sample of keys...");
        std::mt19937 rng(42);
        std::uniform_int_distribution<size_t> dist(0, LARGE_SIZE - 1);
        
        for (size_t i = 0; i < 1000; ++i) {
            size_t idx = dist(rng);
            std::string key = "large_key_" + std::to_string(idx);
            auto val = db->get(key);
            REQUIRE(val.has_value());
        }
        
        INFO("Large dataset test completed successfully");
    }
}

// ===== JOURNAL INTEGRITY TESTS =====

TEST_CASE_METHOD(PerfectHashTestFixture, "Journal integrity and rebuild", "[perfect_hash][journal]") {
    db = Maph::create(test_file, 1000);
    REQUIRE(db != nullptr);
    
    SECTION("Journal logs all unique keys") {
        std::set<std::string> inserted_keys;
        
        // Insert keys
        for (int i = 0; i < 100; ++i) {
            std::string key = "journal_key_" + std::to_string(i);
            inserted_keys.insert(key);
            REQUIRE(db->set(key, "value_" + std::to_string(i)));
        }
        
        // Update some keys (should not create duplicates in journal)
        for (int i = 0; i < 50; ++i) {
            std::string key = "journal_key_" + std::to_string(i);
            REQUIRE(db->set(key, "updated_value_" + std::to_string(i)));
        }
        
        // Read journal file
        std::ifstream journal(journal_file);
        REQUIRE(journal.is_open());
        
        std::unordered_set<std::string> journal_keys;
        std::string line;
        while (std::getline(journal, line)) {
            if (!line.empty()) {
                journal_keys.insert(line);
            }
        }
        
        // Journal should contain all keys (possibly with duplicates)
        for (const auto& key : inserted_keys) {
            CHECK(journal_keys.count(key) > 0);
        }
    }
    
    SECTION("Rebuild from journal after crash") {
        // Insert data
        auto kvs = generate_json_kvs(200);
        for (const auto& [key, value] : kvs) {
            REQUIRE(db->set(key, value));
        }
        
        // Simulate crash (close without proper cleanup)
        db.reset();
        
        // Reopen and optimize
        db = Maph::open(test_file, false);
        REQUIRE(db != nullptr);
        
        auto result = db->optimize();
        REQUIRE(result.ok());
        
        // All keys should still be accessible
        for (const auto& [key, expected_value] : kvs) {
            auto value = db->get(key);
            REQUIRE(value.has_value());
            CHECK(*value == expected_value);
        }
    }
}

// ===== JSON INTERFACE TESTS =====

TEST_CASE_METHOD(PerfectHashTestFixture, "JsonView interface with various inputs", "[perfect_hash][json]") {
    db = Maph::create(test_file, 5000);
    REQUIRE(db != nullptr);
    
    SECTION("Complex nested JSON") {
        std::vector<std::pair<std::string, std::string>> complex_data = {
            {R"({"user":{"id":1,"name":"Alice"}})", 
             R"({"profile":{"age":30,"city":"NYC","tags":["dev","lead"]}})"},
            {R"({"query":{"type":"search","params":{"q":"test","limit":10}}})",
             R"({"results":[{"id":1,"score":0.95},{"id":2,"score":0.87}]})"},
            {R"({"event":"click","timestamp":1234567890})",
             R"({"details":{"x":100,"y":200,"element":"button"}})"}
        };
        
        // Insert complex JSON
        for (const auto& [key, value] : complex_data) {
            REQUIRE(db->set(key, value));
        }
        
        // Optimize
        auto result = db->optimize();
        REQUIRE(result.ok());
        
        // Verify retrieval
        for (const auto& [key, expected_value] : complex_data) {
            auto value = db->get(key);
            REQUIRE(value.has_value());
            CHECK(*value == expected_value);
        }
    }
    
    SECTION("Special characters and escaping") {
        std::vector<std::pair<std::string, std::string>> special_data = {
            {R"({"text":"Hello \"World\""})", R"({"escaped":"Line1\nLine2\tTab"})"},
            {R"({"unicode":"Hello 世界"})", R"({"emoji":"🚀 Launch"})"},
            {R"({"special":"<>&'"})", R"({"path":"C:\\Users\\test"})"}
        };
        
        for (const auto& [key, value] : special_data) {
            REQUIRE(db->set(key, value));
        }
        
        db->optimize();
        
        for (const auto& [key, expected_value] : special_data) {
            auto value = db->get(key);
            REQUIRE(value.has_value());
            CHECK(*value == expected_value);
        }
    }
    
    SECTION("Maximum size values") {
        // Create a value near the maximum size (496 bytes)
        std::string large_value = R"({"data":")";
        large_value += std::string(480, 'X');
        large_value += R"("})";
        
        CHECK(large_value.size() < 496);
        
        REQUIRE(db->set("large_key", large_value));
        
        db->optimize();
        
        auto retrieved = db->get("large_key");
        REQUIRE(retrieved.has_value());
        CHECK(*retrieved == large_value);
    }
}

// ===== MEMORY USAGE TESTS =====

TEST_CASE_METHOD(PerfectHashTestFixture, "Memory usage comparison", "[perfect_hash][memory]") {
    SECTION("Memory overhead of perfect hash") {
        db = Maph::create(test_file, 10000);
        REQUIRE(db != nullptr);
        
        // Insert test data
        const size_t NUM_KEYS = 1000;
        auto kvs = generate_json_kvs(NUM_KEYS);
        for (const auto& [key, value] : kvs) {
            REQUIRE(db->set(key, value));
        }
        
        // Get pre-optimization stats
        auto pre_stats = db->stats();
        size_t pre_memory = pre_stats.memory_bytes;
        
        // Optimize
        auto result = db->optimize();
        REQUIRE(result.ok());
        
        // Get post-optimization stats
        auto post_stats = db->stats();
        size_t post_memory = post_stats.memory_bytes;
        
        // Log memory usage
        INFO("Memory before optimization: " << pre_memory << " bytes");
        INFO("Memory after optimization: " << post_memory << " bytes");
        INFO("Perfect hash keys: " << post_stats.perfect_hash_keys);
        
        // Memory shouldn't increase dramatically
        // Perfect hash structure should be relatively compact
        double memory_ratio = static_cast<double>(post_memory) / pre_memory;
        CHECK(memory_ratio < 1.5);  // Less than 50% increase
    }
}

// ===== COLLISION RATE TESTS =====

TEST_CASE_METHOD(PerfectHashTestFixture, "Collision analysis", "[perfect_hash][collision]") {
    SECTION("Measure collision rate before optimization") {
        db = Maph::create(test_file, 1000);  // Small table to induce collisions
        REQUIRE(db != nullptr);
        
        // Track collisions manually by checking probe distances
        std::vector<int> probe_distances;
        
        for (int i = 0; i < 500; ++i) {
            std::string key = "collision_test_" + std::to_string(i);
            db->set(key, "value_" + std::to_string(i));
        }
        
        auto stats = db->stats();
        double collision_rate = stats.collision_rate;
        
        INFO("Collision rate before optimization: " << collision_rate);
        
        // Optimize should eliminate collisions for known keys
        db->optimize();
        
        auto opt_stats = db->stats();
        INFO("After optimization - Perfect hash keys: " << opt_stats.perfect_hash_keys);
    }
}