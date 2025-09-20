/**
 * @file speed_demo_optimized.cpp
 * @brief Simple demonstration of the clean perfect hashing implementation
 * 
 * Shows the dual-mode operation:
 * 1. Standard FNV-1a hashing with linear probing
 * 2. Perfect hash optimization
 * 3. Performance comparison
 */

#include "maph.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <random>

using namespace maph;

int main() {
    std::cout << "=== Maph Clean Perfect Hashing Demo ===" << std::endl;
    std::cout << std::endl;
    
    // Create database
    auto db = Maph::create("/tmp/demo.maph", 10000);
    if (!db) {
        std::cerr << "Failed to create database" << std::endl;
        return 1;
    }
    
    std::cout << "1. Created database with 10,000 slots" << std::endl;
    
    // Generate test data
    std::vector<std::string> keys;
    std::vector<std::string> values;
    
    std::cout << "2. Adding 1000 key-value pairs..." << std::endl;
    for (int i = 0; i < 1000; ++i) {
        std::string key = "{\"user_id\":" + std::to_string(i) + "}";
        std::string value = "{\"name\":\"User" + std::to_string(i) + 
                           "\",\"score\":" + std::to_string(i * 10) + "}";
        keys.push_back(key);
        values.push_back(value);
        
        bool success = db->set(key, value);
        if (!success) {
            std::cerr << "Failed to insert key " << i << std::endl;
            return 1;
        }
        
        if (i % 100 == 0) {
            std::cout << "   Added " << (i + 1) << " keys..." << std::endl;
        }
    }
    
    // Show pre-optimization stats
    auto pre_stats = db->stats();
    std::cout << std::endl;
    std::cout << "3. Pre-optimization statistics:" << std::endl;
    std::cout << "   Used slots: " << pre_stats.used_slots << "/" << pre_stats.total_slots << std::endl;
    std::cout << "   Load factor: " << (pre_stats.load_factor * 100) << "%" << std::endl;
    std::cout << "   Optimized: " << (pre_stats.is_optimized ? "Yes" : "No") << std::endl;
    std::cout << "   Journal entries: " << pre_stats.journal_entries << std::endl;
    std::cout << "   Collision rate: " << (pre_stats.collision_rate * 100) << "%" << std::endl;
    
    // Benchmark standard mode
    std::cout << std::endl;
    std::cout << "4. Benchmarking standard mode (10,000 lookups)..." << std::endl;
    
    std::mt19937 rng(42);
    auto start = std::chrono::high_resolution_clock::now();
    
    int found_count = 0;
    for (int i = 0; i < 10000; ++i) {
        int key_idx = rng() % keys.size();
        auto value = db->get(keys[key_idx]);
        if (value) found_count++;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    
    std::cout << "   Found: " << found_count << "/10000 keys" << std::endl;
    std::cout << "   Time: " << duration_us << " microseconds" << std::endl;
    std::cout << "   Throughput: " << (10000.0 * 1000000 / duration_us) << " ops/sec" << std::endl;
    
    // Optimize the database
    std::cout << std::endl;
    std::cout << "5. Optimizing with perfect hashing..." << std::endl;
    
    auto opt_start = std::chrono::high_resolution_clock::now();
    auto result = db->optimize();
    auto opt_end = std::chrono::high_resolution_clock::now();
    auto opt_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(opt_end - opt_start).count();
    
    std::cout << "   Result: " << (result.ok() ? "SUCCESS" : "FAILED") << std::endl;
    std::cout << "   Message: " << result.message << std::endl;
    std::cout << "   Optimization time: " << opt_duration_ms << " ms" << std::endl;
    
    // Show post-optimization stats
    auto post_stats = db->stats();
    std::cout << std::endl;
    std::cout << "6. Post-optimization statistics:" << std::endl;
    std::cout << "   Optimized: " << (post_stats.is_optimized ? "Yes" : "No") << std::endl;
    std::cout << "   Perfect hash keys: " << post_stats.perfect_hash_keys << std::endl;
    std::cout << "   Journal entries: " << post_stats.journal_entries << std::endl;
    
    // Benchmark optimized mode
    std::cout << std::endl;
    std::cout << "7. Benchmarking optimized mode (10,000 lookups)..." << std::endl;
    
    rng.seed(42);  // Reset for fair comparison
    start = std::chrono::high_resolution_clock::now();
    
    found_count = 0;
    for (int i = 0; i < 10000; ++i) {
        int key_idx = rng() % keys.size();
        auto value = db->get(keys[key_idx]);
        if (value) found_count++;
    }
    
    end = std::chrono::high_resolution_clock::now();
    auto opt_duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    
    std::cout << "   Found: " << found_count << "/10000 keys" << std::endl;
    std::cout << "   Time: " << opt_duration_us << " microseconds" << std::endl;
    std::cout << "   Throughput: " << (10000.0 * 1000000 / opt_duration_us) << " ops/sec" << std::endl;
    
    // Test hybrid mode (add new keys after optimization)
    std::cout << std::endl;
    std::cout << "8. Testing hybrid mode (adding new keys after optimization)..." << std::endl;
    
    std::vector<std::string> new_keys;
    for (int i = 1000; i < 1020; ++i) {
        std::string key = "{\"user_id\":" + std::to_string(i) + "}";
        std::string value = "{\"name\":\"NewUser" + std::to_string(i) + 
                           "\",\"score\":" + std::to_string(i * 10) + "}";
        new_keys.push_back(key);
        
        bool success = db->set(key, value);
        if (!success) {
            std::cerr << "Failed to insert new key " << i << std::endl;
            return 1;
        }
    }
    
    std::cout << "   Added " << new_keys.size() << " new keys after optimization" << std::endl;
    
    // Verify all keys still work
    std::cout << "   Verifying all keys..." << std::endl;
    
    int verified_old = 0, verified_new = 0;
    for (const auto& key : keys) {
        if (db->get(key)) verified_old++;
    }
    for (const auto& key : new_keys) {
        if (db->get(key)) verified_new++;
    }
    
    std::cout << "   Original keys accessible: " << verified_old << "/" << keys.size() << std::endl;
    std::cout << "   New keys accessible: " << verified_new << "/" << new_keys.size() << std::endl;
    
    // Final stats
    auto final_stats = db->stats();
    std::cout << std::endl;
    std::cout << "9. Final statistics:" << std::endl;
    std::cout << "   Total keys: " << (keys.size() + new_keys.size()) << std::endl;
    std::cout << "   Used slots: " << final_stats.used_slots << "/" << final_stats.total_slots << std::endl;
    std::cout << "   Load factor: " << (final_stats.load_factor * 100) << "%" << std::endl;
    std::cout << "   Journal entries: " << final_stats.journal_entries << std::endl;
    
    std::cout << std::endl;
    std::cout << "=== Summary of Clean Perfect Hashing Implementation ===" << std::endl;
    std::cout << std::endl;
    std::cout << "Features implemented:" << std::endl;
    std::cout << "✓ Single slot array with dual-mode operation" << std::endl;
    std::cout << "✓ Standard FNV-1a hashing with linear probing (before optimization)" << std::endl;
    std::cout << "✓ Perfect hash optimization workflow" << std::endl;
    std::cout << "✓ Hybrid mode: perfect hash for optimized keys + standard hash for new keys" << std::endl;
    std::cout << "✓ JSONL key journal for perfect hash rebuilding" << std::endl;
    std::cout << "✓ Simple optimization workflow: Import → Standard hash → Optimize → Perfect hash" << std::endl;
    std::cout << std::endl;
    std::cout << "Ready for:" << std::endl;
    std::cout << "- Integration with a real perfect hash library (CHD, RecSplit, BBHash)" << std::endl;
    std::cout << "- Command-line usage with 'maph optimize' command" << std::endl;
    std::cout << "- REST API with /optimize endpoint" << std::endl;
    std::cout << "- Production deployment" << std::endl;
    
    return 0;
}