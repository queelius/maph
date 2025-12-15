/**
 * @file v3_demo.cpp
 * @brief Demonstration of the refactored maph v3 API
 *
 * This example showcases the elegant, composable design of maph v3.
 * Each component does one thing well and can be composed orthogonally.
 */

#include <maph/maph.hpp>
#include <iostream>
#include <string>
#include <chrono>
#include <filesystem>

using namespace maph;

// ===== EXAMPLE 1: SIMPLE USAGE =====
void example_simple_api() {
    std::cout << "\n=== Example 1: Simple, Clean API ===\n";

    // Create a database with clear intent
    auto db_result = maph::maph::create("demo.maph", {.slots = slot_count{10000}});
    if (!db_result) {
        std::cerr << "Failed to create database\n";
        return;
    }

    auto& db = *db_result;

    // Clean error handling with monadic operations
    db.set("user:1", R"({"name": "Alice", "age": 30})")
      .and_then([&](auto) { return db.set("user:2", R"({"name": "Bob", "age": 25})"); })
      .and_then([&](auto) { return db.set("user:3", R"({"name": "Carol", "age": 35})"); })
      .or_else([](error e) {
          std::cerr << "Failed to set values\n";
          return status{};
      });

    // Functional get with default
    auto user1 = db.get_or("user:1", "{}");
    std::cout << "User 1: " << user1 << "\n";

    // Check existence
    if (db.contains("user:2")) {
        std::cout << "User 2 exists\n";
    }

    // Functional update
    db.update("user:1", [](std::string_view current) {
        // In real code, we'd parse JSON and update
        return R"({"name": "Alice", "age": 31})";
    });

    std::cout << "Database size: " << db.size() << "\n";
    std::cout << "Load factor: " << db.load_factor() << "\n";
}

// ===== EXAMPLE 2: COMPOSABLE STORAGE =====
void example_composable_storage() {
    std::cout << "\n=== Example 2: Composable Storage Backends ===\n";

    // In-memory storage for testing
    auto memory_db = maph::maph::create_memory({
        .slots = slot_count{1000},
        .enable_cache = true,
        .cache_size = 100
    });

    memory_db.set("test:1", "value1");
    memory_db.set("test:2", "value2");

    // Memory-mapped storage for production
    if (auto mmap_result = maph::maph::create("prod.maph", {.slots = slot_count{100000}})) {
        auto& prod_db = *mmap_result;
        prod_db.set("prod:key", "prod:value");
    }

    std::cout << "Memory DB size: " << memory_db.size() << "\n";
}

// ===== EXAMPLE 3: ORTHOGONAL HASHING =====
void example_orthogonal_hashing() {
    std::cout << "\n=== Example 3: Orthogonal Hashing Strategies ===\n";

    // Different hashers can be composed with any storage
    auto slots = slot_count{1000};

    // Standard FNV-1a hasher
    auto fnv_hasher = fnv1a_hasher{slots};

    // With linear probing
    auto probing_hasher = linear_probe_hasher{fnv_hasher, 20};

    // Create tables with different combinations
    auto heap_storage_instance = heap_storage<>{slots};
    auto table1 = make_table(fnv_hasher, heap_storage<>{slots});
    auto table2 = make_table(probing_hasher, heap_storage<>{slots});

    // Both work identically from the user's perspective
    table1.set("key", "value");
    table2.set("key", "value");

    std::cout << "Table 1 contains 'key': " << table1.contains("key") << "\n";
    std::cout << "Table 2 contains 'key': " << table2.contains("key") << "\n";
}

// ===== EXAMPLE 4: PERFECT HASH OPTIMIZATION =====
void example_perfect_optimization() {
    std::cout << "\n=== Example 4: Perfect Hash Optimization ===\n";

    // Start with standard hash table
    auto slots = slot_count{10000};
    auto standard_table = with_journal(
        make_table(
            linear_probe_hasher{fnv1a_hasher{slots}},
            heap_storage<>{slots}
        )
    );

    // Insert data
    for (int i = 0; i < 100; ++i) {
        auto key = "key:" + std::to_string(i);
        auto value = "value:" + std::to_string(i);
        standard_table.set(key, value);
    }

    std::cout << "Standard table built with " << standard_table.journal().size() << " keys\n";

    // Optimize to perfect hash
    auto perfect_result = standard_table.optimize(heap_storage<>{slots});
    if (perfect_result) {
        std::cout << "Successfully optimized to perfect hash\n";
        auto& perfect_table = *perfect_result;

        // Lookups are now O(1) guaranteed
        if (auto val = perfect_table.get("key:50")) {
            std::cout << "Found key:50 = " << *val << "\n";
        }
    }
}

// ===== EXAMPLE 5: BATCH OPERATIONS =====
void example_batch_operations() {
    std::cout << "\n=== Example 5: Batch Operations ===\n";

    auto db = maph::maph::create_memory({.slots = slot_count{1000}});

    // Transactional batch insert
    auto status = db.set_all({
        {"batch:1", "value1"},
        {"batch:2", "value2"},
        {"batch:3", "value3"},
        {"batch:4", "value4"}
    });

    if (status) {
        std::cout << "Batch insert successful\n";
    }

    // Batch retrieval with callback
    std::vector<std::string_view> keys = {"batch:1", "batch:2", "batch:3"};

    // In real implementation, would have batch get
    for (auto key : keys) {
        if (auto val = db.get(key)) {
            std::cout << key << " = " << *val << "\n";
        }
    }
}

// ===== EXAMPLE 6: ERROR HANDLING =====
void example_error_handling() {
    std::cout << "\n=== Example 6: Elegant Error Handling ===\n";

    // Try to open non-existent file
    auto result = maph::maph::open("/nonexistent/path.maph");

    if (!result) {
        switch (result.error()) {
            case error::io_error:
                std::cout << "IO error occurred\n";
                break;
            case error::invalid_format:
                std::cout << "Invalid file format\n";
                break;
            default:
                std::cout << "Unknown error\n";
        }
    }

    // Monadic error chaining
    auto db = maph::maph::create_memory();

    db.set("key", "value")
      .and_then([&](auto) { return db.set("key2", "value2"); })
      .transform([](auto) {
          std::cout << "All operations successful\n";
      })
      .or_else([](error e) {
          std::cout << "Operation failed\n";
          return status{};
      });
}

// ===== EXAMPLE 7: PERFORMANCE COMPARISON =====
void example_performance() {
    std::cout << "\n=== Example 7: Performance Comparison ===\n";

    const size_t num_keys = 10000;
    auto slots = slot_count{num_keys * 2};

    // Standard hash table
    auto standard_table = make_table(
        linear_probe_hasher{fnv1a_hasher{slots}},
        heap_storage<>{slots}
    );

    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < num_keys; ++i) {
        auto key = std::to_string(i);
        standard_table.set(key, key);
    }
    auto insert_time = std::chrono::high_resolution_clock::now() - start;

    start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < num_keys; ++i) {
        auto key = std::to_string(i);
        [[maybe_unused]] auto val = standard_table.get(key);
    }
    auto lookup_time = std::chrono::high_resolution_clock::now() - start;

    auto insert_us = std::chrono::duration_cast<std::chrono::microseconds>(insert_time).count();
    auto lookup_us = std::chrono::duration_cast<std::chrono::microseconds>(lookup_time).count();

    std::cout << "Standard Hash Table:\n";
    std::cout << "  Insert " << num_keys << " keys: " << insert_us << " µs\n";
    std::cout << "  Lookup " << num_keys << " keys: " << lookup_us << " µs\n";
    std::cout << "  Avg insert: " << insert_us / double(num_keys) << " µs/key\n";
    std::cout << "  Avg lookup: " << lookup_us / double(num_keys) << " µs/key\n";

    auto stats = standard_table.statistics();
    std::cout << "  Load factor: " << stats.load_factor << "\n";
}

// ===== MAIN =====
int main() {
    std::cout << "=== maph v3 Demo - Elegant, Composable Design ===\n";
    std::cout << "Each component does one thing well.\n";
    std::cout << "Components compose orthogonally.\n";
    std::cout << "The API is a joy to use.\n";

    example_simple_api();
    example_composable_storage();
    example_orthogonal_hashing();
    example_perfect_optimization();
    example_batch_operations();
    example_error_handling();
    example_performance();

    std::cout << "\n=== Demo Complete ===\n";

    // Cleanup
    std::filesystem::remove("demo.maph");
    std::filesystem::remove("prod.maph");

    return 0;
}