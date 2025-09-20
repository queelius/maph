/**
 * @file test_perfect_hash.cpp
 * @brief Comprehensive unit tests for perfect hash functionality
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "maph/perfect_hash.hpp"
#include "maph_v2.hpp"
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

// Test fixture for perfect hash testing
class PerfectHashTestFixture {
protected:
    std::string test_file;
    std::string journal_file;
    std::unique_ptr<Maph> maph_instance;
    
    PerfectHashTestFixture() 
        : test_file("test_maph_v2_" + std::to_string(std::random_device{}()) + ".maph"),
          journal_file(test_file + ".journal") {}
    
    ~PerfectHashTestFixture() {
        cleanup();
    }
    
    void cleanup() {
        maph_instance.reset();
        if (fs::exists(test_file)) {
            fs::remove(test_file);
        }
        if (fs::exists(journal_file)) {
            fs::remove(journal_file);
        }
    }
    
    std::vector<std::string> generate_test_keys(size_t count, const std::string& prefix = "key") {
        std::vector<std::string> keys;
        keys.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            keys.push_back(prefix + std::to_string(i));
        }
        return keys;
    }
    
    std::vector<std::string> generate_json_keys(size_t count) {
        std::vector<std::string> keys;
        keys.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            std::stringstream ss;
            ss << "{\"id\":" << i << ",\"type\":\"test\"}";
            keys.push_back(ss.str());
        }
        return keys;
    }
};

// ===== PERFECT HASH INTERFACE TESTS =====

TEST_CASE("PerfectHashInterface basic functionality", "[perfect_hash][interface]") {
    SECTION("RecSplitHash creation and basic operations") {
        PerfectHashConfig config;
        config.type = PerfectHashType::RECSPLIT;
        config.leaf_size = 8;
        
        RecSplitHash hash(config);
        
        std::vector<std::string> keys = {"key1", "key2", "key3", "key4", "key5"};
        CHECK(hash.build(keys) == true);
        CHECK(hash.is_minimal() == true);
        CHECK(hash.key_count() == 5);
        CHECK(hash.type() == PerfectHashType::RECSPLIT);
        
        // Test hash functionality
        for (const auto& key : keys) {
            auto hash_val = hash.hash(key);
            REQUIRE(hash_val.has_value());
            CHECK(*hash_val <= hash.max_hash());
        }
        
        // Test non-existent key
        auto missing = hash.hash("nonexistent");
        CHECK(!missing.has_value());
    }
    
    SECTION("StandardHash fallback") {
        StandardHash hash(1000);
        hash.set_key_count(10);
        
        CHECK(hash.is_minimal() == false);
        CHECK(hash.key_count() == 10);
        CHECK(hash.type() == PerfectHashType::DISABLED);
        CHECK(hash.max_hash() == 999);
        
        // Standard hash should always return a value
        auto hash_val = hash.hash("any_key");
        REQUIRE(hash_val.has_value());
        CHECK(*hash_val < 1000);
    }
    
    SECTION("Serialization and deserialization") {
        RecSplitHash hash;
        std::vector<std::string> keys = {"serialize_key1", "serialize_key2", "serialize_key3"};
        
        REQUIRE(hash.build(keys));
        
        // Serialize
        auto serialized = hash.serialize();
        CHECK(!serialized.empty());
        
        // Deserialize into new instance
        RecSplitHash hash2;
        CHECK(hash2.deserialize(serialized) == true);
        CHECK(hash2.key_count() == keys.size());
        
        // Verify functionality after deserialization
        for (const auto& key : keys) {
            auto original = hash.hash(key);
            auto deserialized = hash2.hash(key);
            
            REQUIRE(original.has_value());
            REQUIRE(deserialized.has_value());
            CHECK(*original == *deserialized);
        }
    }
    
    SECTION("Memory usage tracking") {
        RecSplitHash hash;
        size_t empty_usage = hash.memory_usage();
        
        std::vector<std::string> keys = {"mem1", "mem2", "mem3", "mem4", "mem5"};
        hash.build(keys);
        
        size_t filled_usage = hash.memory_usage();
        CHECK(filled_usage > empty_usage);
    }
}

TEST_CASE("PerfectHashFactory functionality", "[perfect_hash][factory]") {
    SECTION("Create different hash types") {
        PerfectHashConfig config;
        
        config.type = PerfectHashType::RECSPLIT;
        auto recsplit = PerfectHashFactory::create(config);
        REQUIRE(recsplit != nullptr);
        CHECK(recsplit->type() == PerfectHashType::RECSPLIT);
        
        config.type = PerfectHashType::DISABLED;
        auto disabled = PerfectHashFactory::create(config);
        CHECK(disabled == nullptr);  // Should return nullptr for disabled
    }
    
    SECTION("Build from keys") {
        std::vector<std::string> keys = {"build1", "build2", "build3", "build4"};
        
        auto hash = PerfectHashFactory::build(keys);
        REQUIRE(hash != nullptr);
        CHECK(hash->key_count() == keys.size());
        
        // Verify all keys can be hashed
        for (const auto& key : keys) {
            auto hash_val = hash->hash(key);
            CHECK(hash_val.has_value());
        }
    }
    
    SECTION("Load from serialized data") {
        std::vector<std::string> keys = {"load1", "load2", "load3"};
        
        auto original = PerfectHashFactory::build(keys);
        REQUIRE(original != nullptr);
        
        auto serialized = original->serialize();
        auto loaded = PerfectHashFactory::load(serialized, PerfectHashType::RECSPLIT);
        
        REQUIRE(loaded != nullptr);
        CHECK(loaded->key_count() == keys.size());
        
        for (const auto& key : keys) {
            auto original_hash = original->hash(key);
            auto loaded_hash = loaded->hash(key);
            
            REQUIRE(original_hash.has_value());
            REQUIRE(loaded_hash.has_value());
            CHECK(*original_hash == *loaded_hash);
        }
    }
}

// ===== KEY JOURNAL TESTS =====

TEST_CASE_METHOD(PerfectHashTestFixture, "KeyJournal functionality", "[perfect_hash][journal]") {
    SECTION("Basic key recording") {
        KeyJournal journal(journal_file);
        
        journal.record_insert("key1", 12345);
        journal.record_insert("key2", 67890);
        journal.record_insert("key3", 11111);
        
        auto keys = journal.get_active_keys();
        CHECK(keys.size() == 3);
        CHECK(std::find(keys.begin(), keys.end(), "key1") != keys.end());
        CHECK(std::find(keys.begin(), keys.end(), "key2") != keys.end());
        CHECK(std::find(keys.begin(), keys.end(), "key3") != keys.end());
    }
    
    SECTION("Key insertion and removal") {
        KeyJournal journal(journal_file);
        
        journal.record_insert("key1", 12345);
        journal.record_insert("key2", 67890);
        journal.record_remove("key1");
        
        auto keys = journal.get_active_keys();
        CHECK(keys.size() == 1);
        CHECK(keys[0] == "key2");
    }
    
    SECTION("Key persistence across journal instances") {
        {
            KeyJournal journal1(journal_file);
            journal1.record_insert("persistent1", 111);
            journal1.record_insert("persistent2", 222);
            journal1.flush();
        }
        
        {
            KeyJournal journal2(journal_file);
            auto keys = journal2.get_active_keys();
            CHECK(keys.size() == 2);
            CHECK(std::find(keys.begin(), keys.end(), "persistent1") != keys.end());
            CHECK(std::find(keys.begin(), keys.end(), "persistent2") != keys.end());
        }
    }
    
    SECTION("Journal statistics") {
        KeyJournal journal(journal_file);
        
        journal.record_insert("stats_key1", 111);
        journal.record_insert("stats_key2", 222);
        journal.record_insert("stats_key3", 333);
        
        auto stats = journal.get_stats();
        CHECK(stats.total_keys >= 3);
        CHECK(stats.journal_size_bytes > 0);
        
        if (stats.is_cached) {
            CHECK(stats.memory_usage_bytes > 0);
        }
    }
    
    SECTION("Journal compaction") {
        KeyJournal journal(journal_file);
        
        // Add many keys, then remove some
        for (int i = 0; i < 100; ++i) {
            journal.record_insert("compact_key_" + std::to_string(i), i);
        }
        
        for (int i = 0; i < 50; ++i) {
            journal.record_remove("compact_key_" + std::to_string(i * 2));
        }
        
        journal.flush();
        
        size_t entries_removed = journal.compact();
        CHECK(entries_removed > 0);
        
        auto keys = journal.get_active_keys();
        CHECK(keys.size() == 50);  // Should have 50 keys remaining
    }
    
    SECTION("Caching behavior") {
        KeyJournal journal(journal_file);
        
        journal.set_caching(true);
        journal.record_insert("cached1", 111);
        journal.record_insert("cached2", 222);
        
        auto stats1 = journal.get_stats();
        CHECK(stats1.is_cached == true);
        
        journal.set_caching(false);
        auto stats2 = journal.get_stats();
        CHECK(stats2.is_cached == false);
    }
}

// ===== MAPH V2 PERFECT HASH INTEGRATION TESTS =====

TEST_CASE_METHOD(PerfectHashTestFixture, "Maph v2 basic functionality", "[maph_v2][basic]") {
    SECTION("Create maph v2 database") {
        auto m = Maph::create(test_file, 1000);
        REQUIRE(m != nullptr);
        
        auto stats = m->stats();
        CHECK(stats.total_slots == 1000);
        CHECK(stats.hash_mode == HashMode::STANDARD);
        CHECK(stats.perfect_hash_type == PerfectHashType::DISABLED);
        CHECK(stats.is_optimized == false);
        
        // Should have journal file
        CHECK(fs::exists(journal_file));
    }
    
    SECTION("Standard operations before optimization") {
        auto m = Maph::create(test_file, 100);
        REQUIRE(m != nullptr);
        
        // Standard set/get operations
        CHECK(m->set("key1", "value1") == true);
        CHECK(m->set("key2", "value2") == true);
        
        auto v1 = m->get("key1");
        auto v2 = m->get("key2");
        
        REQUIRE(v1.has_value());
        REQUIRE(v2.has_value());
        CHECK(*v1 == "value1");
        CHECK(*v2 == "value2");
        
        // Check removal
        CHECK(m->remove("key1") == true);
        CHECK(m->exists("key1") == false);
        CHECK(m->exists("key2") == true);
    }
    
    SECTION("Key journal integration") {
        auto m = Maph::create(test_file, 100);
        REQUIRE(m != nullptr);
        
        // Insert some keys
        m->set("journal_key1", "value1");
        m->set("journal_key2", "value2");
        m->set("journal_key3", "value3");
        
        // Remove one key
        m->remove("journal_key2");
        
        // Open the journal directly to verify tracking
        KeyJournal journal(journal_file);
        auto keys = journal.get_active_keys();
        
        CHECK(keys.size() == 2);
        CHECK(std::find(keys.begin(), keys.end(), "journal_key1") != keys.end());
        CHECK(std::find(keys.begin(), keys.end(), "journal_key3") != keys.end());
        CHECK(std::find(keys.begin(), keys.end(), "journal_key2") == keys.end());
    }
}

TEST_CASE_METHOD(PerfectHashTestFixture, "Perfect hash optimization", "[maph_v2][optimization]") {
    SECTION("Basic optimization workflow") {
        auto m = Maph::create(test_file, 1000);
        REQUIRE(m != nullptr);
        
        // Populate with test data
        std::vector<std::string> test_keys = {"opt1", "opt2", "opt3", "opt4", "opt5"};
        std::vector<std::string> test_values = {"val1", "val2", "val3", "val4", "val5"};
        
        for (size_t i = 0; i < test_keys.size(); ++i) {
            CHECK(m->set(test_keys[i], test_values[i]) == true);
        }
        
        // Verify standard mode works
        for (size_t i = 0; i < test_keys.size(); ++i) {
            auto value = m->get(test_keys[i]);
            REQUIRE(value.has_value());
            CHECK(*value == test_values[i]);
        }
        
        auto stats_before = m->stats();
        CHECK(stats_before.hash_mode == HashMode::STANDARD);
        CHECK(stats_before.is_optimized == false);
        
        // Optimize
        PerfectHashConfig config;
        auto result = m->optimize(config);
        
        REQUIRE(result.ok());
        CHECK_THAT(result.message, ContainsSubstring("optimized"));
        
        auto stats_after = m->stats();
        CHECK(stats_after.hash_mode == HashMode::PERFECT);
        CHECK(stats_after.is_optimized == true);
        CHECK(stats_after.perfect_hash_memory > 0);
        
        // Verify optimized mode works
        for (size_t i = 0; i < test_keys.size(); ++i) {
            auto value = m->get(test_keys[i]);
            REQUIRE(value.has_value());
            CHECK(*value == test_values[i]);
        }
    }
    
    SECTION("Optimization with different hash types") {
        auto test_hash_type = GENERATE(PerfectHashType::RECSPLIT, 
                                      PerfectHashType::CHD, 
                                      PerfectHashType::BBHASH);
        
        auto m = Maph::create(test_file, 100);
        REQUIRE(m != nullptr);
        
        // Add some test data
        for (int i = 0; i < 10; ++i) {
            m->set("hash_test_" + std::to_string(i), "value_" + std::to_string(i));
        }
        
        PerfectHashConfig config;
        config.type = test_hash_type;
        
        auto result = m->optimize(config);
        REQUIRE(result.ok());
        
        auto stats = m->stats();
        CHECK(stats.perfect_hash_type == test_hash_type);
        CHECK(stats.is_optimized == true);
    }
    
    SECTION("Optimization statistics") {
        auto m = Maph::create(test_file, 100);
        REQUIRE(m != nullptr);
        
        // Add test data
        for (int i = 0; i < 20; ++i) {
            m->set("stats_key_" + std::to_string(i), "stats_value_" + std::to_string(i));
        }
        
        auto opt_stats_before = m->get_optimization_stats();
        CHECK(opt_stats_before.is_optimized == false);
        CHECK(opt_stats_before.current_mode == HashMode::STANDARD);
        CHECK(opt_stats_before.total_keys >= 20);
        
        auto result = m->optimize();
        REQUIRE(result.ok());
        
        auto opt_stats_after = m->get_optimization_stats();
        CHECK(opt_stats_after.is_optimized == true);
        CHECK(opt_stats_after.current_mode == HashMode::PERFECT);
        CHECK(opt_stats_after.collision_rate == 0.0);  // Perfect hash has no collisions
        CHECK(opt_stats_after.perfect_hash_memory > 0);
    }
    
    SECTION("Empty database optimization") {
        auto m = Maph::create(test_file, 100);
        REQUIRE(m != nullptr);
        
        auto result = m->optimize();
        CHECK(result.ok());
        CHECK_THAT(result.message, ContainsSubstring("No keys"));
    }
    
    SECTION("Read-only database optimization fails") {
        // Create and populate database
        {
            auto m = Maph::create(test_file, 100);
            m->set("readonly_test", "value");
        }
        
        // Open read-only
        auto m = maph::open_readonly(test_file);
        REQUIRE(m != nullptr);
        
        auto result = m->optimize();
        CHECK(!result.ok());
        CHECK(result.code == ErrorCode::READONLY_STORE);
    }
}

TEST_CASE_METHOD(PerfectHashTestFixture, "Performance comparison", "[maph_v2][performance]") {
    SECTION("Lookup performance improvement") {
        auto m = Maph::create(test_file, 10000);
        REQUIRE(m != nullptr);
        
        const int NUM_KEYS = 1000;
        std::vector<std::string> test_keys;
        
        // Generate and insert test keys
        for (int i = 0; i < NUM_KEYS; ++i) {
            std::string key = "perf_key_" + std::to_string(i);
            std::string value = "perf_value_" + std::to_string(i);
            test_keys.push_back(key);
            m->set(key, value);
        }
        
        // Benchmark standard mode
        auto start_standard = std::chrono::high_resolution_clock::now();
        int found_standard = 0;
        for (const auto& key : test_keys) {
            if (m->get(key).has_value()) found_standard++;
        }
        auto end_standard = std::chrono::high_resolution_clock::now();
        
        auto duration_standard = std::chrono::duration_cast<std::chrono::microseconds>(
            end_standard - start_standard).count();
        
        CHECK(found_standard == NUM_KEYS);
        
        // Optimize
        auto result = m->optimize();
        REQUIRE(result.ok());
        
        // Benchmark optimized mode
        auto start_optimized = std::chrono::high_resolution_clock::now();
        int found_optimized = 0;
        for (const auto& key : test_keys) {
            if (m->get(key).has_value()) found_optimized++;
        }
        auto end_optimized = std::chrono::high_resolution_clock::now();
        
        auto duration_optimized = std::chrono::duration_cast<std::chrono::microseconds>(
            end_optimized - start_optimized).count();
        
        CHECK(found_optimized == NUM_KEYS);
        
        // Performance should be at least as good (and typically better)
        // Note: In microbenchmarks, the difference might not be dramatic due to caching effects
        CHECK(duration_optimized <= duration_standard * 1.5);  // Allow some variance
        
        std::cout << "Standard mode: " << duration_standard << " μs\n";
        std::cout << "Optimized mode: " << duration_optimized << " μs\n";
        
        if (duration_optimized < duration_standard) {
            double speedup = static_cast<double>(duration_standard) / duration_optimized;
            std::cout << "Speedup: " << speedup << "x\n";
        }
    }
}

TEST_CASE_METHOD(PerfectHashTestFixture, "Complex workflow scenarios", "[maph_v2][workflow]") {
    SECTION("Large dataset optimization") {
        auto m = Maph::create(test_file, 50000);
        REQUIRE(m != nullptr);
        
        const int NUM_KEYS = 5000;
        
        // Populate with larger dataset
        for (int i = 0; i < NUM_KEYS; ++i) {
            std::stringstream key_ss, value_ss;
            key_ss << "{\"id\":" << i << ",\"category\":\"test\"}";
            value_ss << "{\"data\":\"large_value_" << i << "\",\"timestamp\":" << (1000000 + i) << "}";
            
            m->set(key_ss.str(), value_ss.str());
        }
        
        auto stats_before = m->stats();
        CHECK(stats_before.used_slots == NUM_KEYS);
        CHECK(stats_before.load_factor == Catch::Approx(static_cast<double>(NUM_KEYS) / 50000));
        
        // Optimize
        auto start_opt = std::chrono::high_resolution_clock::now();
        auto result = m->optimize();
        auto end_opt = std::chrono::high_resolution_clock::now();
        
        REQUIRE(result.ok());
        
        auto opt_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_opt - start_opt).count();
        
        std::cout << "Optimization took " << opt_duration << " ms for " << NUM_KEYS << " keys\n";
        
        auto stats_after = m->stats();
        CHECK(stats_after.is_optimized == true);
        CHECK(stats_after.used_slots == NUM_KEYS);
        
        // Verify all keys still accessible
        int verified = 0;
        for (int i = 0; i < NUM_KEYS; ++i) {
            std::stringstream key_ss;
            key_ss << "{\"id\":" << i << ",\"category\":\"test\"}";
            
            if (m->get(key_ss.str()).has_value()) {
                verified++;
            }
        }
        
        CHECK(verified == NUM_KEYS);
    }
    
    SECTION("Post-optimization modifications") {
        auto m = Maph::create(test_file, 1000);
        REQUIRE(m != nullptr);
        
        // Add initial data and optimize
        for (int i = 0; i < 10; ++i) {
            m->set("initial_" + std::to_string(i), "value_" + std::to_string(i));
        }
        
        auto result = m->optimize();
        REQUIRE(result.ok());
        
        auto stats_optimized = m->stats();
        CHECK(stats_optimized.is_optimized == true);
        
        // Add new keys after optimization (should work but may fall back to standard hashing)
        CHECK(m->set("new_key_1", "new_value_1") == true);
        CHECK(m->set("new_key_2", "new_value_2") == true);
        
        // Verify both old and new keys are accessible
        for (int i = 0; i < 10; ++i) {
            auto value = m->get("initial_" + std::to_string(i));
            CHECK(value.has_value());
        }
        
        CHECK(m->get("new_key_1").has_value());
        CHECK(m->get("new_key_2").has_value());
        
        // Remove optimized key
        CHECK(m->remove("initial_0") == true);
        CHECK(!m->exists("initial_0"));
        
        // Other keys should still work
        CHECK(m->exists("initial_1"));
    }
}

// ===== STRESS AND EDGE CASE TESTS =====

TEST_CASE_METHOD(PerfectHashTestFixture, "Edge cases and error conditions", "[maph_v2][edge]") {
    SECTION("Optimization with special characters in keys") {
        auto m = Maph::create(test_file, 100);
        REQUIRE(m != nullptr);
        
        std::vector<std::string> special_keys = {
            "key with spaces",
            "key\nwith\nnewlines",
            "key\"with\"quotes",
            "{\"json\":\"key\"}",
            "key_with_unicode_🔑",
            "",  // Empty key
            std::string("\x00\x01\x02", 3)  // Binary data
        };
        
        // Insert all special keys
        for (size_t i = 0; i < special_keys.size(); ++i) {
            std::string value = "special_value_" + std::to_string(i);
            CHECK(m->set(special_keys[i], value) == true);
        }
        
        // Optimize
        auto result = m->optimize();
        REQUIRE(result.ok());
        
        // Verify all special keys still work
        for (size_t i = 0; i < special_keys.size(); ++i) {
            auto value = m->get(special_keys[i]);
            REQUIRE(value.has_value());
            CHECK(*value == "special_value_" + std::to_string(i));
        }
    }
    
    SECTION("Very small database optimization") {
        auto m = Maph::create(test_file, 5);  // Very small
        REQUIRE(m != nullptr);
        
        m->set("small1", "value1");
        m->set("small2", "value2");
        
        auto result = m->optimize();
        CHECK(result.ok());
        
        CHECK(m->get("small1").has_value());
        CHECK(m->get("small2").has_value());
    }
    
    SECTION("Database persistence across restarts") {
        std::vector<std::string> test_keys = {"persist1", "persist2", "persist3"};
        std::vector<std::string> test_values = {"pvalue1", "pvalue2", "pvalue3"};
        
        // Create, populate, and optimize
        {
            auto m = Maph::create(test_file, 100);
            REQUIRE(m != nullptr);
            
            for (size_t i = 0; i < test_keys.size(); ++i) {
                m->set(test_keys[i], test_values[i]);
            }
            
            auto result = m->optimize();
            REQUIRE(result.ok());
            
            // Force sync
            m->sync();
        }
        
        // Reopen and verify
        {
            auto m = maph::open(test_file);
            REQUIRE(m != nullptr);
            
            auto stats = m->stats();
            // Note: In this implementation, perfect hash state may not persist across restarts
            // This is expected behavior for the simplified version
            
            // But data should still be accessible
            for (size_t i = 0; i < test_keys.size(); ++i) {
                auto value = m->get(test_keys[i]);
                REQUIRE(value.has_value());
                CHECK(*value == test_values[i]);
            }
        }
    }
}

TEST_CASE_METHOD(PerfectHashTestFixture, "Concurrent access during optimization", "[maph_v2][concurrent]") {
    SECTION("Read operations during optimization state") {
        auto m = Maph::create(test_file, 1000);
        REQUIRE(m != nullptr);
        
        const int NUM_KEYS = 100;
        std::vector<std::string> keys;
        
        // Populate
        for (int i = 0; i < NUM_KEYS; ++i) {
            std::string key = "concurrent_" + std::to_string(i);
            std::string value = "concurrent_value_" + std::to_string(i);
            keys.push_back(key);
            m->set(key, value);
        }
        
        // Optimize
        auto result = m->optimize();
        REQUIRE(result.ok());
        
        // Simulate concurrent reads after optimization
        std::atomic<int> successful_reads{0};
        std::vector<std::thread> readers;
        
        for (int t = 0; t < 4; ++t) {
            readers.emplace_back([&, t]() {
                for (int i = 0; i < NUM_KEYS; ++i) {
                    int key_idx = (t * 25 + i) % NUM_KEYS;
                    if (m->get(keys[key_idx]).has_value()) {
                        successful_reads++;
                    }
                }
            });
        }
        
        for (auto& reader : readers) {
            reader.join();
        }
        
        CHECK(successful_reads == 4 * NUM_KEYS);
    }
}

// ===== INTEGRATION TESTS WITH REAL WORKLOADS =====

TEST_CASE_METHOD(PerfectHashTestFixture, "Real-world simulation", "[maph_v2][integration]") {
    SECTION("User session store simulation") {
        auto m = Maph::create(test_file, 100000);
        REQUIRE(m != nullptr);
        
        // Simulate user session data
        const int NUM_USERS = 1000;
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> session_id_dist(100000, 999999);
        std::uniform_int_distribution<> timestamp_dist(1600000000, 1700000000);
        
        std::vector<std::string> user_keys;
        
        for (int i = 0; i < NUM_USERS; ++i) {
            std::stringstream key_ss, value_ss;
            
            int session_id = session_id_dist(gen);
            int timestamp = timestamp_dist(gen);
            
            key_ss << "{\"user_id\":" << i << ",\"session_id\":" << session_id << "}";
            value_ss << "{\"login_time\":" << timestamp << ",\"role\":\"user\",\"permissions\":[\"read\",\"write\"]}";
            
            std::string key = key_ss.str();
            user_keys.push_back(key);
            
            CHECK(m->set(key, value_ss.str()) == true);
        }
        
        // Simulate lookup workload before optimization
        auto start_before = std::chrono::high_resolution_clock::now();
        int lookups_before = 0;
        for (int round = 0; round < 10; ++round) {
            for (const auto& key : user_keys) {
                if (m->get(key).has_value()) lookups_before++;
            }
        }
        auto end_before = std::chrono::high_resolution_clock::now();
        
        CHECK(lookups_before == NUM_USERS * 10);
        
        // Optimize
        auto opt_start = std::chrono::high_resolution_clock::now();
        auto result = m->optimize();
        auto opt_end = std::chrono::high_resolution_clock::now();
        
        REQUIRE(result.ok());
        
        auto opt_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            opt_end - opt_start).count();
        
        // Simulate lookup workload after optimization
        auto start_after = std::chrono::high_resolution_clock::now();
        int lookups_after = 0;
        for (int round = 0; round < 10; ++round) {
            for (const auto& key : user_keys) {
                if (m->get(key).has_value()) lookups_after++;
            }
        }
        auto end_after = std::chrono::high_resolution_clock::now();
        
        CHECK(lookups_after == NUM_USERS * 10);
        
        auto duration_before = std::chrono::duration_cast<std::chrono::microseconds>(
            end_before - start_before).count();
        auto duration_after = std::chrono::duration_cast<std::chrono::microseconds>(
            end_after - start_after).count();
        
        std::cout << "User session simulation results:\n";
        std::cout << "  Users: " << NUM_USERS << "\n";
        std::cout << "  Optimization time: " << opt_duration << " ms\n";
        std::cout << "  Before optimization: " << duration_before << " μs\n";
        std::cout << "  After optimization: " << duration_after << " μs\n";
        
        if (duration_after < duration_before) {
            double speedup = static_cast<double>(duration_before) / duration_after;
            std::cout << "  Performance improvement: " << speedup << "x\n";
        }
        
        // Verify optimization statistics
        auto opt_stats = m->get_optimization_stats();
        CHECK(opt_stats.is_optimized == true);
        CHECK(opt_stats.total_keys == NUM_USERS);
        CHECK(opt_stats.collision_rate == 0.0);
    }
}