/**
 * @file test_storage.cpp
 * @brief Comprehensive tests for maph v3 storage backends
 *
 * Tests focus on storage backend contracts:
 * - Data persistence and integrity
 * - Error handling and edge cases
 * - RAII and resource management
 * - Thread safety where applicable
 * - Performance characteristics
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include "maph/v3/storage.hpp"
#include <thread>
#include <vector>
#include <filesystem>
#include <fstream>
#include <random>
#include <algorithm>

using namespace maph::v3;

// ===== TEST UTILITIES =====

// Helper to create test data
std::span<const std::byte> make_test_data(const std::string& str) {
    return {reinterpret_cast<const std::byte*>(str.data()), str.size()};
}

std::string extract_string(const result<value>& val_result) {
    if (!val_result) return "";
    auto bytes = val_result->bytes();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// Unique test file path generator
std::filesystem::path get_test_file_path(const std::string& test_name) {
    static std::atomic<size_t> counter{0};
    auto temp_dir = std::filesystem::temp_directory_path();
    return temp_dir / ("maph_test_" + test_name + "_" + std::to_string(counter++) + ".maph");
}

// RAII cleanup helper
struct temp_file_guard {
    std::filesystem::path path;
    explicit temp_file_guard(std::filesystem::path p) : path(std::move(p)) {}
    ~temp_file_guard() { std::filesystem::remove(path); }
};

// ===== HEAP STORAGE TESTS =====
// Test the in-memory storage backend

TEST_CASE("heap_storage basic operations", "[storage][heap]") {
    const slot_count count{100};
    heap_storage<512> storage{count};

    SECTION("Initial state") {
        REQUIRE(storage.slot_count().value == count.value);

        // All slots should be empty initially
        for (uint64_t i = 0; i < count.value; ++i) {
            slot_index idx{i};
            REQUIRE(storage.empty(idx));
            REQUIRE(storage.hash_at(idx).value == 0);

            auto read_result = storage.read(idx);
            REQUIRE_FALSE(read_result.has_value());
            REQUIRE(read_result.error() == error::key_not_found);
        }
    }

    SECTION("Write and read operations") {
        slot_index idx{42};
        hash_value hash{12345};
        std::string data = "test_data_for_heap_storage";
        auto data_bytes = make_test_data(data);

        // Write data
        auto write_result = storage.write(idx, hash, data_bytes);
        REQUIRE(write_result.has_value());

        // Verify state changes
        REQUIRE_FALSE(storage.empty(idx));
        REQUIRE(storage.hash_at(idx).value == hash.value);

        // Read data back
        auto read_result = storage.read(idx);
        REQUIRE(read_result.has_value());
        REQUIRE(extract_string(read_result) == data);
    }

    SECTION("Clear operation") {
        slot_index idx{10};
        hash_value hash{999};
        auto data_bytes = make_test_data("clear_test_data");

        // Write, then clear
        storage.write(idx, hash, data_bytes);
        REQUIRE_FALSE(storage.empty(idx));

        auto clear_result = storage.clear(idx);
        REQUIRE(clear_result.has_value());

        // Verify cleared state
        REQUIRE(storage.empty(idx));
        REQUIRE_FALSE(storage.read(idx).has_value());
    }

    SECTION("Out of bounds access") {
        slot_index invalid_idx{count.value};  // One past the end

        auto read_result = storage.read(invalid_idx);
        REQUIRE_FALSE(read_result.has_value());
        REQUIRE(read_result.error() == error::key_not_found);

        auto write_result = storage.write(invalid_idx, hash_value{123}, make_test_data("test"));
        REQUIRE_FALSE(write_result.has_value());
        REQUIRE(write_result.error() == error::key_not_found);

        auto clear_result = storage.clear(invalid_idx);
        REQUIRE_FALSE(clear_result.has_value());
        REQUIRE(clear_result.error() == error::key_not_found);
    }

    SECTION("Large data handling") {
        slot_index idx{0};
        hash_value hash{777};

        // Test maximum size data
        std::string max_data(heap_storage<512>::slot_type::data_size, 'X');
        auto max_bytes = make_test_data(max_data);

        auto write_result = storage.write(idx, hash, max_bytes);
        REQUIRE(write_result.has_value());

        auto read_result = storage.read(idx);
        REQUIRE(read_result.has_value());
        REQUIRE(extract_string(read_result) == max_data);

        // Test oversized data
        std::string oversized_data(heap_storage<512>::slot_type::data_size + 1, 'Y');
        auto oversized_bytes = make_test_data(oversized_data);

        auto oversized_result = storage.write(idx, hash, oversized_bytes);
        REQUIRE_FALSE(oversized_result.has_value());
        REQUIRE(oversized_result.error() == error::value_too_large);

        // Original data should be unchanged
        auto unchanged_result = storage.read(idx);
        REQUIRE(unchanged_result.has_value());
        REQUIRE(extract_string(unchanged_result) == max_data);
    }
}

TEST_CASE("heap_storage different slot sizes", "[storage][heap][templates]") {
    SECTION("Small slots") {
        heap_storage<256> small_storage{slot_count{10}};
        REQUIRE(small_storage.slot_count().value == 10);

        slot_index idx{0};
        std::string small_data(200, 'S');  // Should fit
        auto result = small_storage.write(idx, hash_value{123}, make_test_data(small_data));
        REQUIRE(result.has_value());

        auto read_result = small_storage.read(idx);
        REQUIRE(read_result.has_value());
        REQUIRE(extract_string(read_result) == small_data);
    }

    SECTION("Large slots") {
        heap_storage<4096> large_storage{slot_count{5}};
        REQUIRE(large_storage.slot_count().value == 5);

        slot_index idx{0};
        std::string large_data(4000, 'L');  // Should fit in large slot
        auto result = large_storage.write(idx, hash_value{456}, make_test_data(large_data));
        REQUIRE(result.has_value());

        auto read_result = large_storage.read(idx);
        REQUIRE(read_result.has_value());
        REQUIRE(extract_string(read_result) == large_data);
    }
}

// ===== MMAP STORAGE TESTS =====
// Test the memory-mapped file storage backend

TEST_CASE("mmap_storage creation and basic operations", "[storage][mmap]") {
    auto test_path = get_test_file_path("basic_mmap");
    temp_file_guard guard{test_path};
    const slot_count count{50};

    SECTION("File creation and opening") {
        // Create new file
        auto create_result = mmap_storage<>::create(test_path, count);
        REQUIRE(create_result.has_value());

        auto& storage = *create_result;
        REQUIRE(storage.slot_count().value == count.value);

        // File should exist
        REQUIRE(std::filesystem::exists(test_path));
        REQUIRE(std::filesystem::file_size(test_path) > 0);
    }

    SECTION("Write, close, and reopen") {
        slot_index test_idx{25};
        hash_value test_hash{9999};
        std::string test_data = "persistent_data_test";

        // Create and write data
        {
            auto storage = mmap_storage<>::create(test_path, count);
            REQUIRE(storage.has_value());

            auto write_result = storage->write(test_idx, test_hash, make_test_data(test_data));
            REQUIRE(write_result.has_value());

            REQUIRE_FALSE(storage->empty(test_idx));
            REQUIRE(storage->hash_at(test_idx).value == test_hash.value);
        }  // Storage destructor should sync and close

        // Reopen and verify data persisted
        {
            auto storage = mmap_storage<>::open(test_path, false);
            REQUIRE(storage.has_value());

            REQUIRE(storage->slot_count().value == count.value);
            REQUIRE_FALSE(storage->empty(test_idx));
            REQUIRE(storage->hash_at(test_idx).value == test_hash.value);

            auto read_result = storage->read(test_idx);
            REQUIRE(read_result.has_value());
            REQUIRE(extract_string(read_result) == test_data);
        }
    }

    SECTION("Read-only mode") {
        // First create a file with data
        {
            auto storage = mmap_storage<>::create(test_path, count);
            REQUIRE(storage.has_value());
            storage->write(slot_index{0}, hash_value{123}, make_test_data("readonly_test"));
        }

        // Open in read-only mode
        auto readonly_storage = mmap_storage<>::open(test_path, true);
        REQUIRE(readonly_storage.has_value());

        // Reading should work
        auto read_result = readonly_storage->read(slot_index{0});
        REQUIRE(read_result.has_value());
        REQUIRE(extract_string(read_result) == "readonly_test");

        // Writing should fail
        auto write_result = readonly_storage->write(slot_index{1}, hash_value{456}, make_test_data("should_fail"));
        REQUIRE_FALSE(write_result.has_value());
        REQUIRE(write_result.error() == error::permission_denied);

        // Clearing should fail
        auto clear_result = readonly_storage->clear(slot_index{0});
        REQUIRE_FALSE(clear_result.has_value());
        REQUIRE(clear_result.error() == error::permission_denied);
    }
}

TEST_CASE("mmap_storage error conditions", "[storage][mmap][error_handling]") {
    SECTION("Invalid file path") {
        auto invalid_path = std::filesystem::path("/invalid/nonexistent/path.maph");

        auto create_result = mmap_storage<>::create(invalid_path, slot_count{10});
        REQUIRE_FALSE(create_result.has_value());
        REQUIRE(create_result.error() == error::io_error);

        auto open_result = mmap_storage<>::open(invalid_path);
        REQUIRE_FALSE(open_result.has_value());
        REQUIRE(open_result.error() == error::io_error);
    }

    SECTION("Opening non-existent file") {
        auto nonexistent = get_test_file_path("nonexistent");

        auto open_result = mmap_storage<>::open(nonexistent);
        REQUIRE_FALSE(open_result.has_value());
        REQUIRE(open_result.error() == error::io_error);
    }

    SECTION("Invalid file format") {
        auto invalid_file = get_test_file_path("invalid_format");
        temp_file_guard guard{invalid_file};

        // Create a file with wrong content
        {
            std::ofstream out{invalid_file, std::ios::binary};
            out.write("This is not a valid maph file", 29);
        }

        auto open_result = mmap_storage<>::open(invalid_file);
        REQUIRE_FALSE(open_result.has_value());
        REQUIRE(open_result.error() == error::invalid_format);
    }

    SECTION("Truncated file") {
        auto truncated_file = get_test_file_path("truncated");
        temp_file_guard guard{truncated_file};

        // Create a file that's too small
        {
            std::ofstream out{truncated_file, std::ios::binary};
            uint32_t magic = 0x4D415048;  // Correct magic
            out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
            // But don't write the rest of the header
        }

        auto open_result = mmap_storage<>::open(truncated_file);
        // This should fail due to insufficient file size
        REQUIRE_FALSE(open_result.has_value());
    }
}

TEST_CASE("mmap_storage RAII behavior", "[storage][mmap][raii]") {
    auto test_path = get_test_file_path("raii_test");
    temp_file_guard guard{test_path};

    SECTION("Proper cleanup on destruction") {
        {
            auto storage = mmap_storage<>::create(test_path, slot_count{10});
            REQUIRE(storage.has_value());

            // Write some data
            storage->write(slot_index{0}, hash_value{123}, make_test_data("cleanup_test"));
        }  // Storage should properly clean up here

        // File should still exist and be valid
        REQUIRE(std::filesystem::exists(test_path));

        // Should be able to reopen
        auto reopened = mmap_storage<>::open(test_path);
        REQUIRE(reopened.has_value());
    }

    SECTION("Move semantics") {
        auto storage1 = mmap_storage<>::create(test_path, slot_count{10});
        REQUIRE(storage1.has_value());

        // Move constructor
        auto storage2 = std::move(*storage1);

        // Original should be in valid but unspecified state
        // New storage should work
        auto write_result = storage2.write(slot_index{0}, hash_value{456}, make_test_data("move_test"));
        REQUIRE(write_result.has_value());
    }
}

TEST_CASE("mmap_storage concurrent access", "[storage][mmap][threading]") {
    auto test_path = get_test_file_path("concurrent_test");
    temp_file_guard guard{test_path};
    const slot_count count{100};

    SECTION("Multiple readers") {
        // Create file and write test data
        {
            auto storage = mmap_storage<>::create(test_path, count);
            REQUIRE(storage.has_value());

            for (uint64_t i = 0; i < 50; ++i) {
                std::string data = "reader_test_" + std::to_string(i);
                storage->write(slot_index{i}, hash_value{i + 1000}, make_test_data(data));
            }
        }

        // Multiple concurrent readers
        constexpr size_t num_readers = 5;
        std::vector<std::thread> readers;
        std::atomic<size_t> successful_reads{0};

        for (size_t t = 0; t < num_readers; ++t) {
            readers.emplace_back([&, t]() {
                auto storage = mmap_storage<>::open(test_path, true);  // Read-only
                if (!storage) return;

                for (uint64_t i = 0; i < 50; ++i) {
                    auto read_result = storage->read(slot_index{i});
                    if (read_result.has_value()) {
                        std::string expected = "reader_test_" + std::to_string(i);
                        if (extract_string(read_result) == expected) {
                            successful_reads++;
                        }
                    }
                }
            });
        }

        for (auto& reader : readers) {
            reader.join();
        }

        REQUIRE(successful_reads == num_readers * 50);
    }
}

// ===== CACHED STORAGE TESTS =====
// Test the caching decorator

TEST_CASE("cached_storage basic functionality", "[storage][cached]") {
    heap_storage<512> backend{slot_count{100}};
    cached_storage cached{std::move(backend), 10};  // Cache size of 10

    SECTION("Cache miss and hit") {
        slot_index idx{42};
        hash_value hash{555};
        std::string data = "cache_test_data";

        // First read should miss (slot is empty)
        auto miss_result = cached.read(idx);
        REQUIRE_FALSE(miss_result.has_value());

        // Write data
        auto write_result = cached.write(idx, hash, make_test_data(data));
        REQUIRE(write_result.has_value());

        // Read should now hit cache
        auto hit_result = cached.read(idx);
        REQUIRE(hit_result.has_value());
        REQUIRE(extract_string(hit_result) == data);

        // Multiple reads should all hit cache
        for (int i = 0; i < 5; ++i) {
            auto repeat_result = cached.read(idx);
            REQUIRE(repeat_result.has_value());
            REQUIRE(extract_string(repeat_result) == data);
        }
    }

    SECTION("Cache eviction behavior") {
        // Fill cache beyond capacity
        for (size_t i = 0; i < 15; ++i) {  // More than cache size (10)
            slot_index idx{i};
            hash_value hash{i + 1000};
            std::string data = "eviction_test_" + std::to_string(i);

            cached.write(idx, hash, make_test_data(data));
        }

        // All items should still be readable (from backend if not in cache)
        for (size_t i = 0; i < 15; ++i) {
            slot_index idx{i};
            auto read_result = cached.read(idx);
            REQUIRE(read_result.has_value());

            std::string expected = "eviction_test_" + std::to_string(i);
            REQUIRE(extract_string(read_result) == expected);
        }
    }

    SECTION("Cache invalidation on clear") {
        slot_index idx{10};
        hash_value hash{777};
        std::string data = "invalidation_test";

        // Write and cache
        cached.write(idx, hash, make_test_data(data));
        auto cached_read = cached.read(idx);
        REQUIRE(cached_read.has_value());

        // Clear should invalidate cache
        auto clear_result = cached.clear(idx);
        REQUIRE(clear_result.has_value());

        // Read should now miss
        auto post_clear_read = cached.read(idx);
        REQUIRE_FALSE(post_clear_read.has_value());
    }

    SECTION("Write-through behavior") {
        slot_index idx{20};
        hash_value hash{888};
        std::string data = "writethrough_test";

        // Write should go to both cache and backend
        cached.write(idx, hash, make_test_data(data));

        // Clear cache manually
        cached.clear_cache();

        // Data should still be available from backend
        auto read_result = cached.read(idx);
        REQUIRE(read_result.has_value());
        REQUIRE(extract_string(read_result) == data);
    }

    SECTION("Passthrough operations") {
        REQUIRE(cached.slot_count().value == 100);  // Should match backend
    }
}

TEST_CASE("cached_storage with different backends", "[storage][cached][composition]") {
    SECTION("Cached heap storage") {
        auto backend = heap_storage<256>{slot_count{50}};
        auto cached = cached_storage{std::move(backend), 5};

        slot_index idx{0};
        cached.write(idx, hash_value{123}, make_test_data("heap_cached_test"));

        auto result = cached.read(idx);
        REQUIRE(result.has_value());
        REQUIRE(extract_string(result) == "heap_cached_test");
    }

    SECTION("Cached mmap storage") {
        auto test_path = get_test_file_path("cached_mmap");
        temp_file_guard guard{test_path};

        auto mmap_result = mmap_storage<>::create(test_path, slot_count{20});
        REQUIRE(mmap_result.has_value());

        auto cached = cached_storage{std::move(*mmap_result), 3};

        slot_index idx{0};
        cached.write(idx, hash_value{456}, make_test_data("mmap_cached_test"));

        auto result = cached.read(idx);
        REQUIRE(result.has_value());
        REQUIRE(extract_string(result) == "mmap_cached_test");
    }
}

// ===== PROPERTY-BASED TESTS =====
// Test storage backend invariants

TEST_CASE("Storage backend properties", "[storage][properties]") {
    auto backend_type = GENERATE(0, 1);  // 0=heap, 1=mmap
    slot_count count{20};

    SECTION("Write-read consistency property") {
        // Create storage based on type
        if (backend_type == 0) {
            heap_storage<512> storage{count};

            auto slot_idx = GENERATE(take(10, random(0ULL, count.value - 1)));
            auto hash_val = GENERATE(take(5, random(1ULL, 10000ULL)));

            slot_index idx{slot_idx};
            hash_value hash{hash_val};
            std::string data = "property_test_" + std::to_string(hash_val);

            auto write_result = storage.write(idx, hash, make_test_data(data));
            if (write_result.has_value()) {
                auto read_result = storage.read(idx);
                REQUIRE(read_result.has_value());
                REQUIRE(extract_string(read_result) == data);
                REQUIRE(storage.hash_at(idx).value == hash.value);
                REQUIRE_FALSE(storage.empty(idx));
            }
        }
    }

    SECTION("Clear consistency property") {
        heap_storage<512> storage{count};

        auto slot_idx = GENERATE(take(5, random(0ULL, count.value - 1)));
        slot_index idx{slot_idx};

        // Write something first
        storage.write(idx, hash_value{999}, make_test_data("clear_test"));
        REQUIRE_FALSE(storage.empty(idx));

        // Clear and verify
        auto clear_result = storage.clear(idx);
        REQUIRE(clear_result.has_value());
        REQUIRE(storage.empty(idx));
        REQUIRE_FALSE(storage.read(idx).has_value());
    }
}

// ===== PERFORMANCE TESTS =====
// Benchmark storage operations

TEST_CASE("Storage performance benchmarks", "[storage][performance][!benchmark]") {
    const slot_count count{10000};
    heap_storage<512> storage{count};

    // Prepare test data
    std::vector<std::pair<slot_index, std::string>> test_data;
    test_data.reserve(1000);
    for (size_t i = 0; i < 1000; ++i) {
        test_data.emplace_back(
            slot_index{i},
            "benchmark_data_" + std::to_string(i) + "_" + std::string(100, 'X')
        );
    }

    BENCHMARK("Heap storage writes") {
        for (const auto& [idx, data] : test_data) {
            storage.write(idx, hash_value{idx.value + 1}, make_test_data(data));
        }
    };

    // Write all data first for read benchmark
    for (const auto& [idx, data] : test_data) {
        storage.write(idx, hash_value{idx.value + 1}, make_test_data(data));
    }

    BENCHMARK("Heap storage reads") {
        for (const auto& [idx, data] : test_data) {
            auto result = storage.read(idx);
            (void)result;  // Prevent optimization
        }
    };
}

TEST_CASE("Cached storage performance", "[storage][cached][performance][!benchmark]") {
    heap_storage<512> backend{slot_count{10000}};
    cached_storage cached{std::move(backend), 100};

    // Prepare data
    std::vector<slot_index> indices;
    for (size_t i = 0; i < 200; ++i) {
        indices.emplace_back(i);
        std::string data = "cached_benchmark_" + std::to_string(i);
        cached.write(slot_index{i}, hash_value{i + 1}, make_test_data(data));
    }

    BENCHMARK("Cached reads (hot cache)") {
        // Read first 100 items repeatedly (should be in cache)
        for (size_t i = 0; i < 100; ++i) {
            auto result = cached.read(slot_index{i});
            (void)result;
        }
    };

    BENCHMARK("Cached reads (cold cache)") {
        // Read last 100 items (likely not in cache due to eviction)
        for (size_t i = 100; i < 200; ++i) {
            auto result = cached.read(slot_index{i});
            (void)result;
        }
    };
}

// ===== STRESS TESTS =====
// Test storage under extreme conditions

TEST_CASE("Storage stress tests", "[storage][stress]") {
    SECTION("Many small operations") {
        heap_storage<512> storage{slot_count{1000}};

        // Perform many random operations
        std::mt19937 rng{42};
        std::uniform_int_distribution<uint64_t> slot_dist{0, 999};
        std::uniform_int_distribution<int> op_dist{0, 2};  // 0=write, 1=read, 2=clear

        for (size_t i = 0; i < 10000; ++i) {
            slot_index idx{slot_dist(rng)};
            int op = op_dist(rng);

            switch (op) {
                case 0: {  // Write
                    std::string data = "stress_" + std::to_string(i);
                    storage.write(idx, hash_value{i + 1}, make_test_data(data));
                    break;
                }
                case 1: {  // Read
                    auto result = storage.read(idx);
                    (void)result;  // Don't care about result
                    break;
                }
                case 2: {  // Clear
                    storage.clear(idx);
                    break;
                }
            }
        }

        // No crashes = success
        REQUIRE(true);
    }

    SECTION("Large data stress test") {
        heap_storage<4096> large_storage{slot_count{100}};

        // Write maximum-size data to all slots
        std::string large_data(heap_storage<4096>::slot_type::data_size, 'L');

        for (uint64_t i = 0; i < 100; ++i) {
            slot_index idx{i};
            hash_value hash{i + 1000};

            auto result = large_storage.write(idx, hash, make_test_data(large_data));
            REQUIRE(result.has_value());
        }

        // Verify all data
        for (uint64_t i = 0; i < 100; ++i) {
            slot_index idx{i};
            auto result = large_storage.read(idx);
            REQUIRE(result.has_value());
            REQUIRE(extract_string(result) == large_data);
        }
    }
}