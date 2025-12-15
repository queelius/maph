/**
 * @file v3_simple_test.cpp
 * @brief Simple test to verify v3 compiles and runs
 */

#include <maph/maph.hpp>
#include <iostream>
#include <filesystem>

using namespace maph;

int main() {
    std::cout << "=== maph v3 Simple Test ===\n\n";

    // Test 1: In-memory storage
    {
        std::cout << "Test 1: In-memory storage\n";

        // Create table directly without maph wrapper
        auto slots = slot_count{100};
        auto hasher = fnv1a_hasher{slots};
        auto storage = heap_storage<>{slots};
        auto table = make_table(hasher, storage);

        // Test operations
        auto status1 = table.set("key1", "value1");
        auto status2 = table.set("key2", "value2");

        if (status1 && status2) {
            std::cout << "  Set operations successful\n";
        }

        auto val1 = table.get("key1");
        if (val1) {
            std::cout << "  Retrieved key1: " << *val1 << "\n";
        }

        if (table.contains("key2")) {
            std::cout << "  key2 exists\n";
        }

        auto stats = table.statistics();
        std::cout << "  Used slots: " << stats.used_slots
                  << "/" << stats.total_slots.value
                  << " (load factor: " << stats.load_factor << ")\n";
    }

    // Test 2: Memory-mapped storage
    {
        std::cout << "\nTest 2: Memory-mapped storage\n";

        auto path = std::filesystem::path("test.maph");
        auto slots = slot_count{100};

        // Create mmap storage
        auto storage_result = mmap_storage<>::create(path, slots);
        if (!storage_result) {
            std::cerr << "  Failed to create mmap storage\n";
            return 1;
        }

        // Create table with mmap storage
        auto hasher = fnv1a_hasher{slots};
        auto table = make_table(hasher, std::move(*storage_result));

        // Test operations
        table.set("mmap_key1", "mmap_value1");
        table.set("mmap_key2", "mmap_value2");

        if (auto val = table.get("mmap_key1")) {
            std::cout << "  Retrieved mmap_key1: " << *val << "\n";
        }

        auto stats = table.statistics();
        std::cout << "  Used slots: " << stats.used_slots << "/" << stats.total_slots.value << "\n";

        // Cleanup
        std::filesystem::remove(path);
    }

    // Test 3: Linear probing
    {
        std::cout << "\nTest 3: Linear probing\n";

        auto slots = slot_count{50};
        auto base_hasher = fnv1a_hasher{slots};
        auto probing_hasher = linear_probe_hasher{base_hasher, 10};
        auto storage = heap_storage<>{slots};
        auto table = make_table(probing_hasher, storage);

        // Insert many items to test probing
        int inserted = 0;
        for (int i = 0; i < 40; ++i) {
            auto key = std::to_string(i);
            if (table.set(key, key)) {
                inserted++;
            }
        }

        std::cout << "  Inserted " << inserted << " items\n";

        // Verify some retrievals
        if (auto val = table.get("10")) {
            std::cout << "  Retrieved key '10': " << *val << "\n";
        }
    }

    // Test 4: Perfect hashing (simplified)
    {
        std::cout << "\nTest 4: Perfect hashing\n";

        minimal_perfect_hasher::builder builder;
        builder.add("perfect1")
               .add("perfect2")
               .add("perfect3");

        auto hasher_result = builder.build();
        if (!hasher_result) {
            std::cerr << "  Failed to build perfect hash\n";
        } else {
            auto& hasher = *hasher_result;

            std::cout << "  Perfect hash built for "
                      << hasher.max_slots().value << " keys\n";

            if (hasher.is_perfect_for("perfect1")) {
                std::cout << "  'perfect1' is in perfect set\n";
            }

            if (!hasher.is_perfect_for("unknown")) {
                std::cout << "  'unknown' is not in perfect set\n";
            }

            if (auto slot = hasher.slot_for("perfect2")) {
                std::cout << "  'perfect2' maps to slot " << slot->value << "\n";
            }
        }
    }

    std::cout << "\n=== All tests completed successfully ===\n";
    return 0;
}