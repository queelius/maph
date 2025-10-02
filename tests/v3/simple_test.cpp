// Simple test runner for v3 code without external dependencies
#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include "maph/v3/core.hpp"
#include "maph/v3/hashers.hpp"
#include "maph/v3/storage.hpp"
#include "maph/v3/table.hpp"
#include "maph/v3/maph.hpp"

using namespace maph::v3;

void test_core_types() {
    std::cout << "Testing core types..." << std::endl;

    // Test strong types
    slot_index idx{42};
    assert(idx.value == 42);

    hash_value hash{0x12345678};
    assert(hash.value == 0x12345678);

    slot_count count{100};
    assert(count.value == 100);

    std::cout << "  ✓ Strong types work" << std::endl;

    // Test slot
    slot s;
    s.set("test_key", "test_value");
    assert(s.occupied());
    auto [k, v] = s.get();
    assert(k == "test_key");
    assert(v == "test_value");

    std::cout << "  ✓ Slot operations work" << std::endl;
}

void test_hashers() {
    std::cout << "Testing hashers..." << std::endl;

    slot_count slots{100};
    fnv1a_hasher hasher{slots};

    auto idx1 = hasher("key1");
    auto idx2 = hasher("key2");
    auto idx3 = hasher("key1");  // Same key

    assert(idx1 == idx3);  // Deterministic
    assert(idx1.value < 100);  // Within bounds
    assert(idx2.value < 100);

    std::cout << "  ✓ FNV-1a hasher works" << std::endl;

    // Test linear probe hasher
    linear_probe_hasher probe_hasher{hasher, 20};
    auto result = probe_hasher("test");
    assert(result.has_value());

    std::cout << "  ✓ Linear probe hasher works" << std::endl;
}

void test_storage() {
    std::cout << "Testing storage..." << std::endl;

    heap_storage storage{slot_count{10}};

    // Test set/get
    slot s;
    s.set("key", "value");
    storage.store(slot_index{0}, s);

    auto loaded = storage.load(slot_index{0});
    assert(loaded.has_value());
    assert(loaded->occupied());

    std::cout << "  ✓ Heap storage works" << std::endl;

    // Test cached storage
    cached_storage cached{storage, 5};
    cached.store(slot_index{1}, s);
    auto cached_load = cached.load(slot_index{1});
    assert(cached_load.has_value());

    std::cout << "  ✓ Cached storage works" << std::endl;
}

void test_table() {
    std::cout << "Testing table..." << std::endl;

    slot_count slots{100};
    auto table = make_table(fnv1a_hasher{slots}, heap_storage{slots});

    // Test basic operations
    auto set_result = table.set("test_key", "test_value");
    assert(set_result.has_value());

    auto get_result = table.get("test_key");
    assert(get_result.has_value());
    assert(*get_result == "test_value");

    std::cout << "  ✓ Table basic operations work" << std::endl;

    // Test with linear probing
    auto probe_table = make_table(
        linear_probe_hasher{fnv1a_hasher{slots}, 20},
        heap_storage{slots}
    );

    probe_table.set("key1", "value1");
    probe_table.set("key2", "value2");

    assert(probe_table.get("key1").value_or("") == "value1");
    assert(probe_table.get("key2").value_or("") == "value2");

    std::cout << "  ✓ Table with linear probing works" << std::endl;
}

void test_high_level() {
    std::cout << "Testing high-level maph interface..." << std::endl;

    maph_config config{
        .slots = 1000,
        .hasher_type = hasher_type::fnv1a,
        .storage_type = storage_type::heap,
        .enable_cache = true,
        .cache_size = 100
    };

    auto db = maph::make(config);

    // Test operations
    db.set("user:1", R"({"name": "Alice", "age": 30})");
    db.set("user:2", R"({"name": "Bob", "age": 25})");

    auto user1 = db.get("user:1");
    assert(user1.has_value());
    assert(user1->find("Alice") != std::string::npos);

    std::cout << "  ✓ High-level maph interface works" << std::endl;

    // Test batch operations
    std::vector<std::pair<std::string, std::string>> batch = {
        {"key1", "value1"},
        {"key2", "value2"},
        {"key3", "value3"}
    };

    auto batch_result = db.set_batch(batch);
    assert(batch_result.has_value());

    auto keys = std::vector<std::string>{"key1", "key2", "key3"};
    auto values = db.get_batch(keys);
    assert(values.size() == 3);

    std::cout << "  ✓ Batch operations work" << std::endl;
}

int main() {
    std::cout << "\n=== Running maph v3 Tests ===\n" << std::endl;

    try {
        test_core_types();
        test_hashers();
        test_storage();
        test_table();
        test_high_level();

        std::cout << "\n✅ All tests passed!\n" << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "\n❌ Test failed: " << e.what() << std::endl;
        return 1;
    }
}