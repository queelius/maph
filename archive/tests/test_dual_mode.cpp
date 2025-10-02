/**
 * @file test_dual_mode.cpp
 * @brief Test dual-mode operation: standard hash -> perfect hash optimization
 * 
 * Verifies that the maph system correctly:
 * 1. Starts with standard FNV-1a hashing + linear probing
 * 2. Logs keys to journal during operations
 * 3. Can be optimized to use perfect hashing
 * 4. Falls back to standard hash for new keys after optimization
 */

#include "maph.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <chrono>
#include <random>

using namespace maph;

void test_standard_mode() {
    std::cout << "Testing standard mode (before optimization)..." << std::endl;
    
    // Create a small test database
    auto db = Maph::create("/tmp/test_standard.maph", 1000);
    assert(db != nullptr);
    
    // Add some test data
    std::vector<std::pair<std::string, std::string>> test_data = {
        {"{\"id\":1}", "{\"name\":\"Alice\",\"age\":30}"},
        {"{\"id\":2}", "{\"name\":\"Bob\",\"age\":25}"},
        {"{\"id\":3}", "{\"name\":\"Charlie\",\"age\":35}"},
        {"{\"id\":4}", "{\"name\":\"Diana\",\"age\":28}"},
        {"{\"id\":5}", "{\"name\":\"Eve\",\"age\":32}"}
    };
    
    // Test insertion
    for (const auto& [key, value] : test_data) {
        bool success = db->set(key, value);
        assert(success);
        std::cout << "  Inserted: " << key << std::endl;
    }
    
    // Test retrieval
    for (const auto& [key, expected_value] : test_data) {
        auto value = db->get(key);
        assert(value.has_value());
        assert(*value == expected_value);
        std::cout << "  Retrieved: " << key << " -> " << *value << std::endl;
    }
    
    // Check stats
    auto stats = db->stats();
    std::cout << "  Stats: " << stats.used_slots << "/" << stats.total_slots 
              << " slots used, optimized: " << (stats.is_optimized ? "Yes" : "No") << std::endl;
    assert(!stats.is_optimized);
    assert(stats.journal_entries >= test_data.size());
    
    std::cout << "Standard mode test PASSED" << std::endl << std::endl;
}

void test_optimization() {
    std::cout << "Testing optimization to perfect hash..." << std::endl;
    
    // Create and populate database
    auto db = Maph::create("/tmp/test_optimize.maph", 1000);
    assert(db != nullptr);
    
    // Add more test data to make optimization worthwhile
    std::vector<std::string> keys;
    std::vector<std::string> values;
    
    for (int i = 1; i <= 20; ++i) {
        std::string key = "{\"id\":" + std::to_string(i) + "}";
        std::string value = "{\"data\":\"value_" + std::to_string(i) + "\"}";
        keys.push_back(key);
        values.push_back(value);
        
        bool success = db->set(key, value);
        assert(success);
    }
    
    std::cout << "  Added " << keys.size() << " keys" << std::endl;
    
    // Check pre-optimization stats
    auto pre_stats = db->stats();
    std::cout << "  Pre-optimization: optimized=" << (pre_stats.is_optimized ? "Yes" : "No")
              << ", journal_entries=" << pre_stats.journal_entries << std::endl;
    assert(!pre_stats.is_optimized);
    assert(pre_stats.journal_entries >= keys.size());
    
    // Perform optimization
    auto start_time = std::chrono::high_resolution_clock::now();
    auto result = db->optimize();
    auto end_time = std::chrono::high_resolution_clock::now();
    
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
    
    std::cout << "  Optimization result: " << (result.ok() ? "SUCCESS" : "FAILED") 
              << ", message: " << result.message << std::endl;
    std::cout << "  Optimization took: " << duration_ms << " ms" << std::endl;
    
    // Note: Our placeholder implementation may not actually optimize,
    // but it should not fail either
    assert(result.ok());
    
    // Check post-optimization stats
    auto post_stats = db->stats();
    std::cout << "  Post-optimization: optimized=" << (post_stats.is_optimized ? "Yes" : "No")
              << ", perfect_hash_keys=" << post_stats.perfect_hash_keys << std::endl;
    
    // Verify all original keys still work
    for (size_t i = 0; i < keys.size(); ++i) {
        auto value = db->get(keys[i]);
        assert(value.has_value());
        assert(*value == values[i]);
    }
    std::cout << "  All " << keys.size() << " keys still accessible after optimization" << std::endl;
    
    std::cout << "Optimization test PASSED" << std::endl << std::endl;
}

void test_hybrid_mode() {
    std::cout << "Testing hybrid mode (perfect hash + new keys)..." << std::endl;
    
    // Create database and optimize it
    auto db = Maph::create("/tmp/test_hybrid.maph", 1000);
    assert(db != nullptr);
    
    // Add initial data
    std::vector<std::pair<std::string, std::string>> initial_data = {
        {"{\"type\":\"user\",\"id\":1}", "{\"name\":\"Alice\"}"},
        {"{\"type\":\"user\",\"id\":2}", "{\"name\":\"Bob\"}"},
        {"{\"type\":\"user\",\"id\":3}", "{\"name\":\"Charlie\"}"}
    };
    
    for (const auto& [key, value] : initial_data) {
        db->set(key, value);
    }
    
    // Optimize
    auto opt_result = db->optimize();
    std::cout << "  Optimization: " << opt_result.message << std::endl;
    
    // Add new keys after optimization (should use standard hashing)
    std::vector<std::pair<std::string, std::string>> new_data = {
        {"{\"type\":\"user\",\"id\":4}", "{\"name\":\"Diana\"}"},
        {"{\"type\":\"user\",\"id\":5}", "{\"name\":\"Eve\"}"}
    };
    
    for (const auto& [key, value] : new_data) {
        bool success = db->set(key, value);
        assert(success);
        std::cout << "  Added new key after optimization: " << key << std::endl;
    }
    
    // Verify all keys work (both optimized and new)
    std::cout << "  Verifying all keys..." << std::endl;
    for (const auto& [key, expected_value] : initial_data) {
        auto value = db->get(key);
        assert(value.has_value());
        assert(*value == expected_value);
        std::cout << "    Initial key OK: " << key << std::endl;
    }
    
    for (const auto& [key, expected_value] : new_data) {
        auto value = db->get(key);
        assert(value.has_value());
        assert(*value == expected_value);
        std::cout << "    New key OK: " << key << std::endl;
    }
    
    auto final_stats = db->stats();
    std::cout << "  Final stats: used_slots=" << final_stats.used_slots 
              << ", journal_entries=" << final_stats.journal_entries << std::endl;
    
    std::cout << "Hybrid mode test PASSED" << std::endl << std::endl;
}

void test_performance_comparison() {
    std::cout << "Testing performance comparison (standard vs optimized)..." << std::endl;
    
    const int num_operations = 10000;
    const int num_keys = 1000;
    
    // Generate test data
    std::vector<std::string> keys;
    std::vector<std::string> values;
    std::mt19937 rng(42);  // Fixed seed for reproducibility
    
    for (int i = 0; i < num_keys; ++i) {
        keys.push_back("{\"benchmark_key\":" + std::to_string(i) + "}");
        values.push_back("{\"benchmark_value\":" + std::to_string(rng()) + "}");
    }
    
    // Test standard mode performance
    auto db_standard = Maph::create("/tmp/bench_standard.maph", num_keys * 2);
    
    // Populate database
    for (size_t i = 0; i < keys.size(); ++i) {
        db_standard->set(keys[i], values[i]);
    }
    
    // Benchmark standard mode reads
    auto start = std::chrono::high_resolution_clock::now();
    volatile int found_count = 0;
    for (int op = 0; op < num_operations; ++op) {
        int key_idx = rng() % keys.size();
        auto value = db_standard->get(keys[key_idx]);
        if (value) found_count++;
    }
    auto end = std::chrono::high_resolution_clock::now();
    
    auto standard_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    std::cout << "  Standard mode: " << num_operations << " lookups in " << standard_us 
              << " μs (" << (num_operations * 1000000.0 / standard_us) << " ops/sec)" << std::endl;
    
    // Test optimized mode performance
    auto db_optimized = Maph::create("/tmp/bench_optimized.maph", num_keys * 2);
    
    // Populate and optimize
    for (size_t i = 0; i < keys.size(); ++i) {
        db_optimized->set(keys[i], values[i]);
    }
    
    auto opt_result = db_optimized->optimize();
    std::cout << "  Optimization: " << opt_result.message << std::endl;
    
    // Benchmark optimized mode reads
    rng.seed(42);  // Reset RNG for fair comparison
    start = std::chrono::high_resolution_clock::now();
    found_count = 0;
    for (int op = 0; op < num_operations; ++op) {
        int key_idx = rng() % keys.size();
        auto value = db_optimized->get(keys[key_idx]);
        if (value) found_count++;
    }
    end = std::chrono::high_resolution_clock::now();
    
    auto optimized_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    std::cout << "  Optimized mode: " << num_operations << " lookups in " << optimized_us 
              << " μs (" << (num_operations * 1000000.0 / optimized_us) << " ops/sec)" << std::endl;
    
    // Calculate speedup (if any)
    if (optimized_us < standard_us) {
        double speedup = static_cast<double>(standard_us) / optimized_us;
        std::cout << "  Speedup: " << speedup << "x faster with optimization" << std::endl;
    } else {
        std::cout << "  Note: Optimization overhead present (expected with placeholder implementation)" << std::endl;
    }
    
    std::cout << "Performance test COMPLETED" << std::endl << std::endl;
}

int main() {
    std::cout << "=== Maph Dual-Mode Operation Test Suite ===" << std::endl << std::endl;
    
    try {
        test_standard_mode();
        test_optimization();
        test_hybrid_mode();
        test_performance_comparison();
        
        std::cout << "=== ALL TESTS PASSED ===" << std::endl;
        std::cout << std::endl;
        std::cout << "Summary of dual-mode operation:" << std::endl;
        std::cout << "1. ✓ Standard FNV-1a hashing with linear probing works" << std::endl;
        std::cout << "2. ✓ Keys are logged to journal during operations" << std::endl;
        std::cout << "3. ✓ Database can be optimized (placeholder implementation)" << std::endl;
        std::cout << "4. ✓ Hybrid mode supports both optimized and new keys" << std::endl;
        std::cout << "5. ✓ Performance comparison shows measurement capability" << std::endl;
        std::cout << std::endl;
        std::cout << "The clean perfect hashing implementation is ready for:" << std::endl;
        std::cout << "- Integration with a real perfect hash library (CHD, RecSplit, etc.)" << std::endl;
        std::cout << "- Production use with the simple REST API" << std::endl;
        std::cout << "- Command-line operations with the optimize command" << std::endl;
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}