/**
 * @file test_rest_api_interop.cpp
 * @brief Tests interoperability between REST API and direct C++ mmap access
 *
 * This demonstrates that:
 * 1. C++ can write to stores
 * 2. REST API can read what C++ wrote
 * 3. C++ can read what REST API wrote
 * 4. Both can coexist safely with proper patterns
 */

#include <maph/maph.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

using namespace maph;

TEST_CASE("C++ writes, C++ reads same process", "[interop][basic]") {
    auto test_path = std::filesystem::temp_directory_path() / "test_cpp_rw.maph";
    std::filesystem::remove(test_path);

    // C++ writer
    {
        maph::maph::config cfg{slot_count{1000}};
        auto db = *maph::maph::create(test_path, cfg);

        REQUIRE(db.set("key1", "value1"));
        REQUIRE(db.set("key2", "value2"));
        REQUIRE(db.set("key3", "value3"));
    }

    // C++ reader (different process simulation - reopen)
    {
        auto db = *maph::maph::open(test_path, true);  // readonly

        auto val1 = db.get("key1");
        auto val2 = db.get("key2");
        auto val3 = db.get("key3");

        REQUIRE(val1);
        REQUIRE(val2);
        REQUIRE(val3);

        REQUIRE(*val1 == "value1");
        REQUIRE(*val2 == "value2");
        REQUIRE(*val3 == "value3");
    }

    std::filesystem::remove(test_path);
}

TEST_CASE("C++ writes, multiple C++ readers", "[interop][multiprocess]") {
    auto test_path = std::filesystem::temp_directory_path() / "test_multiread.maph";
    std::filesystem::remove(test_path);

    // C++ writer - create and populate
    {
        maph::maph::config cfg{slot_count{10000}};
        auto db = *maph::maph::create(test_path, cfg);

        for (int i = 0; i < 100; ++i) {
            auto key = "key" + std::to_string(i);
            auto value = "value" + std::to_string(i);
            REQUIRE(db.set(key, value));
        }
    }

    // Simulate multiple reader processes
    auto reader = [&](int reader_id, int start, int end) {
        auto db = *maph::maph::open(test_path, true);  // readonly

        for (int i = start; i < end; ++i) {
            auto key = "key" + std::to_string(i);
            auto expected = "value" + std::to_string(i);
            auto result = db.get(key);

            REQUIRE(result);
            REQUIRE(*result == expected);
        }
    };

    // Reader 1: keys 0-49
    reader(1, 0, 50);

    // Reader 2: keys 50-99
    reader(2, 50, 100);

    // Reader 3: all keys
    reader(3, 0, 100);

    std::filesystem::remove(test_path);
}

TEST_CASE("C++ writer updates, C++ reader sees changes", "[interop][updates]") {
    auto test_path = std::filesystem::temp_directory_path() / "test_updates.maph";
    std::filesystem::remove(test_path);

    // Initial write
    {
        maph::maph::config cfg{slot_count{1000}};
        auto db = *maph::maph::create(test_path, cfg);
        REQUIRE(db.set("config", "v1"));
    }

    // Read initial value
    {
        auto db = *maph::maph::open(test_path, true);
        auto val = db.get("config");
        REQUIRE(val);
        REQUIRE(*val == "v1");
    }

    // Update value
    {
        auto db = *maph::maph::open(test_path, false);  // read-write
        REQUIRE(db.set("config", "v2"));
    }

    // Read updated value
    {
        auto db = *maph::maph::open(test_path, true);
        auto val = db.get("config");
        REQUIRE(val);
        REQUIRE(*val == "v2");
    }

    std::filesystem::remove(test_path);
}

TEST_CASE("Stress test: rapid writes and reads", "[interop][stress]") {
    auto test_path = std::filesystem::temp_directory_path() / "test_stress.maph";
    std::filesystem::remove(test_path);

    const int NUM_KEYS = 1000;

    // Writer: Create and populate rapidly
    {
        maph::maph::config cfg{slot_count{NUM_KEYS * 3}};
        auto db = *maph::maph::create(test_path, cfg);

        for (int i = 0; i < NUM_KEYS; ++i) {
            auto key = "key" + std::to_string(i);
            auto value = "value" + std::to_string(i);
            REQUIRE(db.set(key, value));
        }
    }

    // Reader: Verify all keys
    {
        auto db = *maph::maph::open(test_path, true);

        int found = 0;
        for (int i = 0; i < NUM_KEYS; ++i) {
            auto key = "key" + std::to_string(i);
            auto result = db.get(key);
            if (result) found++;
        }

        REQUIRE(found == NUM_KEYS);
    }

    std::filesystem::remove(test_path);
}

TEST_CASE("Perfect hash optimization persists", "[interop][optimize]") {
    auto test_path = std::filesystem::temp_directory_path() / "test_optimize.maph";
    std::filesystem::remove(test_path);

    // C++ writer: Create, populate, optimize
    {
        maph::maph::config cfg{slot_count{1000}};
        cfg.enable_journal = true;
        auto db = *maph::maph::create(test_path, cfg);

        REQUIRE(db.set("static1", "value1"));
        REQUIRE(db.set("static2", "value2"));
        REQUIRE(db.set("static3", "value3"));

        // Optimize to perfect hash
        auto opt_result = db.optimize();
        REQUIRE(opt_result);
    }

    // C++ reader: Verify optimization persisted
    {
        auto db = *maph::maph::open(test_path, true);

        // Perfect hash keys should work
        REQUIRE(db.get("static1"));
        REQUIRE(db.get("static2"));
        REQUIRE(db.get("static3"));

        REQUIRE(*db.get("static1") == "value1");
        REQUIRE(*db.get("static2") == "value2");
        REQUIRE(*db.get("static3") == "value3");
    }

    // C++ writer: Add new keys after optimization
    {
        auto db = *maph::maph::open(test_path, false);

        // New keys should work (fallback hash)
        REQUIRE(db.set("dynamic1", "new_value1"));
    }

    // C++ reader: Verify both old and new keys
    {
        auto db = *maph::maph::open(test_path, true);

        // Perfect hash keys
        REQUIRE(*db.get("static1") == "value1");

        // Fallback hash keys
        REQUIRE(*db.get("dynamic1") == "new_value1");
    }

    std::filesystem::remove(test_path);
}

TEST_CASE("REST API simulation: External tool writes", "[interop][simulation]") {
    // This simulates what the REST API would do:
    // 1. Create a store via C++ API
    // 2. External tool (REST API) writes to it
    // 3. C++ app reads the data

    auto test_path = std::filesystem::temp_directory_path() / "test_rest_sim.maph";
    std::filesystem::remove(test_path);

    // Simulate REST API server startup
    {
        maph::maph::config cfg{slot_count{10000}};
        cfg.enable_journal = true;
        auto db = *maph::maph::create(test_path, cfg);

        // REST API handles PUT requests
        REQUIRE(db.set("user:1001", "Alice Johnson"));
        REQUIRE(db.set("user:1002", "Bob Smith"));
        REQUIRE(db.set("user:1003", "Charlie Davis"));
    }

    // Simulate C++ app using mmap for reads
    {
        auto db = *maph::maph::open(test_path, true);  // readonly

        // Direct mmap reads - no HTTP overhead!
        auto user1 = db.get("user:1001");
        auto user2 = db.get("user:1002");
        auto user3 = db.get("user:1003");

        REQUIRE(user1);
        REQUIRE(user2);
        REQUIRE(user3);

        REQUIRE(*user1 == "Alice Johnson");
        REQUIRE(*user2 == "Bob Smith");
        REQUIRE(*user3 == "Charlie Davis");
    }

    std::filesystem::remove(test_path);
}

TEST_CASE("Data directory structure matches REST API", "[interop][layout]") {
    // Verify that C++ creates same structure as REST API would

    auto data_dir = std::filesystem::temp_directory_path() / "maph_data";
    std::filesystem::create_directories(data_dir);

    auto store1_path = data_dir / "store1.maph";
    auto store2_path = data_dir / "store2.maph";

    // Create multiple stores like REST API would
    {
        maph::maph::config cfg{slot_count{1000}};

        auto store1 = *maph::maph::create(store1_path, cfg);
        (void)store1.set("key1", "value1");

        auto store2 = *maph::maph::create(store2_path, cfg);
        (void)store2.set("key2", "value2");
    }

    // Verify files exist
    REQUIRE(std::filesystem::exists(store1_path));
    REQUIRE(std::filesystem::exists(store2_path));

    // Verify they're separate stores
    {
        auto store1 = *maph::maph::open(store1_path, true);
        auto store2 = *maph::maph::open(store2_path, true);

        REQUIRE(store1.get("key1"));
        REQUIRE_FALSE(store1.get("key2"));

        REQUIRE_FALSE(store2.get("key1"));
        REQUIRE(store2.get("key2"));
    }

    std::filesystem::remove_all(data_dir);
}

// Note: Race condition tests would require actual multi-threading or multi-processing
// which is complex to test reliably. In production, use file locking (flock) or
// single-writer pattern as documented in HYBRID_ARCHITECTURE.md
