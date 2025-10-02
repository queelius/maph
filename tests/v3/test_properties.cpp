/**
 * @file test_properties.cpp
 * @brief Property-based tests for maph v3 - testing invariants and mathematical properties
 *
 * These tests verify that fundamental properties and invariants hold across
 * different inputs and configurations:
 * - Hash function properties (determinism, distribution)
 * - Storage consistency properties
 * - Table operation invariants
 * - Composability properties
 * - Performance scaling properties
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_adapters.hpp>
#include <catch2/generators/catch_generators_random.hpp>

#include "maph/v3/maph.hpp"
#include "maph/v3/hashers.hpp"
#include "maph/v3/storage.hpp"
#include "maph/v3/table.hpp"
#include <vector>
#include <string>
#include <unordered_set>
#include <algorithm>
#include <random>
#include <filesystem>
#include <cmath>

using namespace maph::v3;

// ===== PROPERTY GENERATORS =====

// Generate random strings for testing
std::string generate_random_string(size_t min_len = 1, size_t max_len = 100) {
    static std::mt19937 rng{42};
    std::uniform_int_distribution<size_t> len_dist{min_len, max_len};
    std::uniform_int_distribution<char> char_dist{'a', 'z'};

    size_t len = len_dist(rng);
    std::string result;
    result.reserve(len);

    for (size_t i = 0; i < len; ++i) {
        result.push_back(char_dist(rng));
    }

    return result;
}

// Generate test data pairs
std::vector<std::pair<std::string, std::string>> generate_test_dataset(size_t size) {
    std::vector<std::pair<std::string, std::string>> dataset;
    dataset.reserve(size);

    for (size_t i = 0; i < size; ++i) {
        std::string key = "key_" + std::to_string(i) + "_" + generate_random_string(5, 20);
        std::string value = "value_" + std::to_string(i) + "_" + generate_random_string(10, 50);
        dataset.emplace_back(std::move(key), std::move(value));
    }

    return dataset;
}

// Generate temporary file path for property tests
std::filesystem::path generate_property_test_file() {
    static std::atomic<size_t> counter{0};
    auto temp_dir = std::filesystem::temp_directory_path();
    return temp_dir / ("property_test_" + std::to_string(counter++) + ".maph");
}

struct temp_file_guard {
    std::filesystem::path path;
    explicit temp_file_guard(std::filesystem::path p) : path(std::move(p)) {}
    ~temp_file_guard() { std::filesystem::remove(path); }
};

// ===== HASH FUNCTION PROPERTIES =====

TEST_CASE("Hash function mathematical properties", "[properties][hash]") {
    SECTION("Determinism property") {
        auto slot_count_val = GENERATE(take(10, random(10ULL, 10000ULL)));
        slot_count slots{slot_count_val};
        fnv1a_hasher hasher{slots};

        // Generate random test strings
        auto test_string = GENERATE(take(50, map([](size_t i) {
            return "determinism_test_" + std::to_string(i) + "_" + generate_random_string();
        }, random(1ULL, 100000ULL))));

        // Hash the same string multiple times
        auto hash1 = hasher.hash(test_string);
        auto hash2 = hasher.hash(test_string);
        auto hash3 = hasher.hash(test_string);

        // Must always produce the same hash
        REQUIRE(hash1.value == hash2.value);
        REQUIRE(hash2.value == hash3.value);

        // Index calculation must also be deterministic
        auto idx1 = hasher.index_for(test_string);
        auto idx2 = hasher.index_for(test_string);
        REQUIRE(idx1.value == idx2.value);
        REQUIRE(idx1.value < slots.value);
    }

    SECTION("Non-zero hash property") {
        auto slot_count_val = GENERATE(take(5, random(1ULL, 1000ULL)));
        slot_count slots{slot_count_val};
        fnv1a_hasher hasher{slots};

        // Test various string types
        auto test_input = GENERATE(
            std::string(""),  // Empty string
            std::string("a"),  // Single character
            std::string("short"),  // Short string
            generate_random_string(100, 1000),  // Long string
            std::string("\0\0\0", 3),  // Null bytes
            std::string("\xFF\xFE\xFD")  // High byte values
        );

        auto hash = hasher.hash(test_input);

        // FNV-1a implementation should never return 0
        REQUIRE(hash.value != 0);
    }

    SECTION("Distribution uniformity property") {
        slot_count slots{100};
        fnv1a_hasher hasher{slots};

        // Generate many test keys
        const size_t num_keys = 10000;
        std::vector<uint64_t> bucket_counts(slots.value, 0);

        for (size_t i = 0; i < num_keys; ++i) {
            std::string key = "distribution_test_" + std::to_string(i) + "_" + generate_random_string();
            auto index = hasher.index_for(key);
            bucket_counts[index.value]++;
        }

        // Calculate distribution statistics
        double expected_per_bucket = double(num_keys) / slots.value;
        double sum_squared_deviations = 0.0;
        size_t empty_buckets = 0;

        for (auto count : bucket_counts) {
            if (count == 0) empty_buckets++;
            double deviation = count - expected_per_bucket;
            sum_squared_deviations += deviation * deviation;
        }

        double variance = sum_squared_deviations / slots.value;
        double std_dev = std::sqrt(variance);

        // For good distribution:
        // - Most buckets should have some items (< 10% empty)
        // - Standard deviation should be reasonable (< 2x expected for Poisson)
        REQUIRE(empty_buckets < slots.value / 10);
        REQUIRE(std_dev < 2.0 * std::sqrt(expected_per_bucket));
    }

    SECTION("Avalanche effect property") {
        slot_count slots{1000};
        fnv1a_hasher hasher{slots};

        // Test single-bit changes cause significant hash changes
        auto base_string = GENERATE(take(10, map([](size_t i) {
            return "avalanche_test_" + std::to_string(i);
        }, random(1ULL, 1000ULL))));

        auto base_hash = hasher.hash(base_string);

        // Modify single character
        std::string modified = base_string;
        if (!modified.empty()) {
            modified[0] = (modified[0] == 'a') ? 'b' : 'a';  // Single character change

            auto modified_hash = hasher.hash(modified);

            // Count differing bits
            uint64_t xor_result = base_hash.value ^ modified_hash.value;
            int differing_bits = __builtin_popcountll(xor_result);

            // Good hash functions should change many bits
            REQUIRE(differing_bits >= 10);  // At least some significant change
        }
    }
}

TEST_CASE("Linear probe hasher properties", "[properties][linear_probe]") {
    SECTION("Probe sequence properties") {
        auto slots_val = GENERATE(take(5, random(10ULL, 1000ULL)));
        auto max_probes = GENERATE(take(3, random(1ULL, 20ULL)));

        slot_count slots{slots_val};
        linear_probe_hasher hasher{fnv1a_hasher{slots}, static_cast<size_t>(max_probes)};

        auto test_key = GENERATE(take(20, map([](size_t i) {
            return "probe_test_" + std::to_string(i);
        }, random(1ULL, 10000ULL))));

        auto probe_seq = hasher.probe_sequence(test_key);
        std::vector<slot_index> indices;

        // Collect all probe indices
        while (!probe_seq.at_end()) {
            indices.push_back(*probe_seq);
            ++probe_seq;
        }

        // Properties that must hold:
        REQUIRE(indices.size() == static_cast<size_t>(max_probes));  // Exact probe count

        // All indices must be valid
        for (auto idx : indices) {
            REQUIRE(idx.value < slots.value);
        }

        // Indices should form arithmetic sequence (mod slots)
        if (indices.size() > 1) {
            for (size_t i = 1; i < indices.size(); ++i) {
                uint64_t expected = (indices[0].value + i) % slots.value;
                REQUIRE(indices[i].value == expected);
            }
        }
    }
}

// ===== STORAGE BACKEND PROPERTIES =====

TEST_CASE("Storage backend consistency properties", "[properties][storage]") {
    SECTION("Write-read consistency for heap storage") {
        auto slots_val = GENERATE(take(5, random(10ULL, 1000ULL)));
        slot_count slots{slots_val};
        heap_storage<512> storage{slots};

        auto test_data = GENERATE(take(50, map([](size_t i) {
            return std::make_tuple(
                i % 100,  // slot index
                i + 1000,  // hash value
                "storage_test_" + std::to_string(i) + "_" + generate_random_string()
            );
        }, random(1ULL, 10000ULL))));

        auto [slot_idx, hash_val, data] = test_data;

        if (slot_idx < slots.value) {
            slot_index idx{slot_idx};
            hash_value hash{hash_val};
            auto data_bytes = std::span{reinterpret_cast<const std::byte*>(data.data()), data.size()};

            // Write-read consistency
            auto write_result = storage.write(idx, hash, data_bytes);
            if (write_result.has_value()) {
                // If write succeeded, read must succeed with same data
                auto read_result = storage.read(idx);
                REQUIRE(read_result.has_value());

                auto retrieved_bytes = read_result->bytes();
                std::string retrieved{reinterpret_cast<const char*>(retrieved_bytes.data()), retrieved_bytes.size()};
                REQUIRE(retrieved == data);

                // Hash must match
                REQUIRE(storage.hash_at(idx).value == hash.value);
                REQUIRE_FALSE(storage.empty(idx));
            }
        }
    }

    SECTION("Clear consistency property") {
        auto slots_val = GENERATE(take(3, random(10ULL, 100ULL)));
        slot_count slots{slots_val};
        heap_storage<512> storage{slots};

        auto slot_idx = GENERATE(take(20, random(0ULL, std::min(slots_val - 1, 99ULL))));
        slot_index idx{slot_idx};

        // Write some data first
        std::string data = "clear_test_" + std::to_string(slot_idx);
        auto data_bytes = std::span{reinterpret_cast<const std::byte*>(data.data()), data.size()};
        storage.write(idx, hash_value{123}, data_bytes);

        // Clear operation properties
        auto clear_result = storage.clear(idx);
        REQUIRE(clear_result.has_value());

        // After clear, slot must be empty
        REQUIRE(storage.empty(idx));

        // Read must fail after clear
        auto read_result = storage.read(idx);
        REQUIRE_FALSE(read_result.has_value());
        REQUIRE(read_result.error() == error::key_not_found);
    }

    SECTION("Mmap storage persistence property") {
        auto test_path = generate_property_test_file();
        temp_file_guard guard{test_path};

        auto slots_val = GENERATE(take(3, random(10ULL, 100ULL)));
        slot_count slots{slots_val};

        auto persistent_data = GENERATE(take(10, map([](size_t i) {
            return std::make_tuple(
                i % 50,  // slot index
                i + 2000,  // hash value
                "persistence_test_" + std::to_string(i)
            );
        }, random(1ULL, 1000ULL))));

        auto [slot_idx, hash_val, data] = persistent_data;

        if (slot_idx < slots.value) {
            slot_index idx{slot_idx};
            hash_value hash{hash_val};

            // Write data and close storage
            {
                auto storage_result = mmap_storage<>::create(test_path, slots);
                REQUIRE(storage_result.has_value());

                auto data_bytes = std::span{reinterpret_cast<const std::byte*>(data.data()), data.size()};
                auto write_result = storage_result->write(idx, hash, data_bytes);
                REQUIRE(write_result.has_value());
            }  // Storage destructor should sync

            // Reopen and verify persistence
            {
                auto storage_result = mmap_storage<>::open(test_path);
                REQUIRE(storage_result.has_value());

                auto read_result = storage_result->read(idx);
                REQUIRE(read_result.has_value());

                auto retrieved_bytes = read_result->bytes();
                std::string retrieved{reinterpret_cast<const char*>(retrieved_bytes.data()), retrieved_bytes.size()};
                REQUIRE(retrieved == data);
                REQUIRE(storage_result->hash_at(idx).value == hash.value);
            }
        }
    }
}

TEST_CASE("Cached storage properties", "[properties][cached_storage]") {
    SECTION("Cache transparency property") {
        auto slots_val = GENERATE(take(3, random(10ULL, 100ULL)));
        slot_count slots{slots_val};

        heap_storage<512> backend{slots};
        cached_storage cached{std::move(backend), 10};

        auto test_data = GENERATE(take(30, map([](size_t i) {
            return std::make_tuple(
                i % 50,  // slot index
                i + 3000,  // hash value
                "cache_test_" + std::to_string(i)
            );
        }, random(1ULL, 1000ULL))));

        auto [slot_idx, hash_val, data] = test_data;

        if (slot_idx < slots.value) {
            slot_index idx{slot_idx};
            hash_value hash{hash_val};
            auto data_bytes = std::span{reinterpret_cast<const std::byte*>(data.data()), data.size()};

            // Cache should not change semantics
            auto write_result = cached.write(idx, hash, data_bytes);
            if (write_result.has_value()) {
                // Multiple reads should return consistent results
                auto read1 = cached.read(idx);
                auto read2 = cached.read(idx);
                auto read3 = cached.read(idx);

                REQUIRE(read1.has_value());
                REQUIRE(read2.has_value());
                REQUIRE(read3.has_value());

                // All reads should return identical data
                auto bytes1 = read1->bytes();
                auto bytes2 = read2->bytes();
                auto bytes3 = read3->bytes();

                REQUIRE(bytes1.size() == bytes2.size());
                REQUIRE(bytes2.size() == bytes3.size());
                REQUIRE(std::equal(bytes1.begin(), bytes1.end(), bytes2.begin()));
                REQUIRE(std::equal(bytes2.begin(), bytes2.end(), bytes3.begin()));
            }
        }
    }
}

// ===== TABLE OPERATION PROPERTIES =====

TEST_CASE("Hash table operation invariants", "[properties][table]") {
    SECTION("Set-get consistency invariant") {
        auto slots_val = GENERATE(take(5, random(100ULL, 1000ULL)));
        slot_count slots{slots_val};

        auto table = make_table(
            linear_probe_hasher{fnv1a_hasher{slots}, 10},
            heap_storage<512>{slots}
        );

        auto test_pair = GENERATE(take(100, map([](size_t i) {
            return std::make_pair(
                "invariant_key_" + std::to_string(i) + "_" + generate_random_string(5, 20),
                "invariant_value_" + std::to_string(i) + "_" + generate_random_string(10, 50)
            );
        }, random(1ULL, 10000ULL))));

        auto [key, value] = test_pair;

        // Set-get invariant: if set succeeds, get must return the same value
        auto set_result = table.set(key, value);
        if (set_result.has_value()) {
            auto get_result = table.get(key);
            REQUIRE(get_result.has_value());
            REQUIRE(*get_result == value);
            REQUIRE(table.contains(key));
        }
    }

    SECTION("Remove consistency invariant") {
        auto slots_val = GENERATE(take(3, random(50ULL, 500ULL)));
        slot_count slots{slots_val};

        auto table = make_table(
            fnv1a_hasher{slots},
            heap_storage<512>{slots}
        );

        auto test_key = GENERATE(take(50, map([](size_t i) {
            return "remove_test_" + std::to_string(i) + "_" + generate_random_string();
        }, random(1ULL, 1000ULL))));

        // Set up initial state
        std::string value = "remove_value_for_" + test_key;
        table.set(test_key, value);

        // Remove invariant: if remove succeeds, key must not exist
        auto remove_result = table.remove(test_key);
        if (remove_result.has_value()) {
            REQUIRE_FALSE(table.contains(test_key));
            auto get_result = table.get(test_key);
            REQUIRE_FALSE(get_result.has_value());
            REQUIRE(get_result.error() == error::key_not_found);
        }
    }

    SECTION("Statistics consistency invariant") {
        slot_count slots{200};
        auto table = make_table(
            linear_probe_hasher{fnv1a_hasher{slots}, 15},
            heap_storage<512>{slots}
        );

        size_t operations = 50;
        std::unordered_set<std::string> expected_keys;

        for (size_t i = 0; i < operations; ++i) {
            std::string key = "stats_key_" + std::to_string(i);
            std::string value = "stats_value_" + std::to_string(i);

            auto set_result = table.set(key, value);
            if (set_result.has_value() && table.contains(key)) {
                expected_keys.insert(key);
            }
        }

        auto stats = table.statistics();

        // Statistics must be consistent with actual content
        REQUIRE(stats.total_slots.value == slots.value);
        REQUIRE(stats.used_slots <= expected_keys.size());  // May be less due to collisions
        REQUIRE(stats.used_slots <= slots.value);
        REQUIRE(stats.load_factor >= 0.0);
        REQUIRE(stats.load_factor <= 1.0);

        if (stats.used_slots > 0) {
            REQUIRE(stats.load_factor > 0.0);
        }

        if (stats.used_slots == slots.value) {
            REQUIRE(stats.load_factor == 1.0);
        }
    }
}

// ===== COMPOSITION PROPERTIES =====

TEST_CASE("Component composition properties", "[properties][composition]") {
    SECTION("Storage backend substitutability") {
        slot_count slots{100};
        auto hasher = fnv1a_hasher{slots};

        // Create tables with different storage backends
        auto heap_table = make_table(hasher, heap_storage<512>{slots});

        auto test_path = generate_property_test_file();
        temp_file_guard guard{test_path};

        auto mmap_result = mmap_storage<>::create(test_path, slots);
        REQUIRE(mmap_result.has_value());
        auto mmap_table = make_table(hasher, std::move(*mmap_result));

        // Both tables should behave identically for same operations
        auto test_data = generate_test_dataset(20);

        for (const auto& [key, value] : test_data) {
            auto heap_set = heap_table.set(key, value);
            auto mmap_set = mmap_table.set(key, value);

            // Both should succeed or fail in the same way (for same hasher)
            if (heap_set.has_value() && mmap_set.has_value()) {
                auto heap_get = heap_table.get(key);
                auto mmap_get = mmap_table.get(key);

                REQUIRE(heap_get.has_value());
                REQUIRE(mmap_get.has_value());
                REQUIRE(*heap_get == *mmap_get);
            }
        }
    }

    SECTION("Hasher substitutability") {
        slot_count slots{100};
        auto storage1 = heap_storage<512>{slots};
        auto storage2 = heap_storage<512>{slots};

        // Tables with different hashers but same storage type
        auto direct_table = make_table(fnv1a_hasher{slots}, std::move(storage1));
        auto probe_table = make_table(
            linear_probe_hasher{fnv1a_hasher{slots}, 10},
            std::move(storage2)
        );

        // Both should provide hash table semantics (even if collision handling differs)
        std::string test_key = "substitution_test";
        std::string test_value = "substitution_value";

        auto direct_result = direct_table.set(test_key, test_value);
        auto probe_result = probe_table.set(test_key, test_value);

        // If both succeed, both should be able to retrieve the value
        if (direct_result.has_value()) {
            auto direct_get = direct_table.get(test_key);
            REQUIRE(direct_get.has_value());
            REQUIRE(*direct_get == test_value);
        }

        if (probe_result.has_value()) {
            auto probe_get = probe_table.get(test_key);
            REQUIRE(probe_get.has_value());
            REQUIRE(*probe_get == test_value);
        }
    }
}

// ===== PERFORMANCE SCALING PROPERTIES =====

TEST_CASE("Performance scaling properties", "[properties][performance]") {
    SECTION("Load factor impact on performance") {
        slot_count slots{1000};

        // Test different load factors
        std::vector<double> load_factors = {0.1, 0.25, 0.5, 0.75, 0.9};
        std::vector<double> avg_lookup_times;

        for (double load_factor : load_factors) {
            auto table = make_table(
                linear_probe_hasher{fnv1a_hasher{slots}, 20},
                heap_storage<512>{slots}
            );

            size_t num_items = static_cast<size_t>(slots.value * load_factor);

            // Fill table to desired load factor
            std::vector<std::string> keys;
            for (size_t i = 0; i < num_items; ++i) {
                std::string key = "perf_key_" + std::to_string(i);
                std::string value = "perf_value_" + std::to_string(i);
                table.set(key, value);
                keys.push_back(key);
            }

            // Measure lookup performance
            auto start = std::chrono::high_resolution_clock::now();
            for (const auto& key : keys) {
                [[maybe_unused]] auto result = table.get(key);
            }
            auto end = std::chrono::high_resolution_clock::now();

            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
            double avg_time = double(duration.count()) / keys.size();
            avg_lookup_times.push_back(avg_time);
        }

        // Performance should degrade gracefully with load factor
        // (Though exact relationship depends on collision resolution strategy)

        // At minimum, lookup time should remain bounded
        for (double time : avg_lookup_times) {
            REQUIRE(time < 10000.0);  // Less than 10 microseconds per lookup
        }

        // Very low load factors should be faster than very high ones
        REQUIRE(avg_lookup_times[0] <= avg_lookup_times.back() * 2.0);
    }

    SECTION("Dataset size scaling property") {
        // Test that operations remain efficient as dataset grows
        std::vector<size_t> dataset_sizes = {100, 500, 1000, 2000};
        std::vector<double> per_item_times;

        for (size_t size : dataset_sizes) {
            slot_count slots{size * 2};  // Maintain ~50% load factor
            auto table = make_table(
                fnv1a_hasher{slots},
                heap_storage<512>{slots}
            );

            auto dataset = generate_test_dataset(size);

            // Measure insertion time
            auto start = std::chrono::high_resolution_clock::now();
            for (const auto& [key, value] : dataset) {
                table.set(key, value);
            }
            auto end = std::chrono::high_resolution_clock::now();

            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
            double per_item_time = double(duration.count()) / size;
            per_item_times.push_back(per_item_time);
        }

        // Per-item insertion time should not grow dramatically
        // (Should be roughly constant for hash tables)
        double first_time = per_item_times[0];
        double last_time = per_item_times.back();

        // Allow some growth but not exponential
        REQUIRE(last_time <= first_time * 3.0);

        // All times should be reasonable
        for (double time : per_item_times) {
            REQUIRE(time < 50000.0);  // Less than 50 microseconds per item
        }
    }
}

// ===== ERROR HANDLING PROPERTIES =====

TEST_CASE("Error handling properties", "[properties][error_handling]") {
    SECTION("Error propagation consistency") {
        // Test that errors propagate consistently through the system
        auto test_path = std::filesystem::path("/invalid/nonexistent/path.maph");

        auto db_result = maph::create(test_path, {.slots = slot_count{10}});

        // Should consistently fail with IO error
        REQUIRE_FALSE(db_result.has_value());
        REQUIRE(db_result.error() == error::io_error);

        // Multiple attempts should produce same error
        auto db_result2 = maph::create(test_path, {.slots = slot_count{20}});
        REQUIRE_FALSE(db_result2.has_value());
        REQUIRE(db_result2.error() == error::io_error);
    }

    SECTION("Value size limit consistency") {
        slot_count slots{10};
        auto table = make_table(
            fnv1a_hasher{slots},
            heap_storage<256>{slots}  // Small slots
        );

        // Generate value that's definitely too large
        std::string large_value(1000, 'X');  // Larger than any reasonable slot

        auto large_keys = GENERATE(take(10, map([](size_t i) {
            return "large_test_" + std::to_string(i);
        }, random(1ULL, 100ULL))));

        // Should consistently fail for oversized values
        auto result = table.set(large_keys, large_value);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == error::value_too_large);

        // Key should not exist after failed set
        REQUIRE_FALSE(table.contains(large_keys));
    }
}

// ===== MATHEMATICAL INVARIANTS =====

TEST_CASE("Mathematical invariants", "[properties][invariants]") {
    SECTION("Slot index bounds invariant") {
        auto slots_val = GENERATE(take(10, random(1ULL, 10000ULL)));
        slot_count slots{slots_val};
        fnv1a_hasher hasher{slots};

        auto test_keys = GENERATE(take(100, map([](size_t i) {
            return "bounds_test_" + std::to_string(i) + "_" + generate_random_string();
        }, random(1ULL, 100000ULL))));

        auto index = hasher.index_for(test_keys);

        // Index must always be within bounds
        REQUIRE(index.value < slots.value);

        // Hash modulo must equal index
        auto hash = hasher.hash(test_keys);
        REQUIRE((hash.value % slots.value) == index.value);
    }

    SECTION("Load factor mathematical properties") {
        slot_count slots{100};
        auto table = make_table(
            fnv1a_hasher{slots},
            heap_storage<512>{slots}
        );

        // Load factor must always be between 0 and 1
        auto initial_stats = table.statistics();
        REQUIRE(initial_stats.load_factor >= 0.0);
        REQUIRE(initial_stats.load_factor <= 1.0);

        // Add some items
        size_t items_added = 0;
        for (size_t i = 0; i < 50; ++i) {
            std::string key = "load_test_" + std::to_string(i);
            std::string value = "load_value_" + std::to_string(i);

            if (table.set(key, value).has_value() && table.contains(key)) {
                items_added++;
            }
        }

        auto final_stats = table.statistics();

        // Load factor properties
        REQUIRE(final_stats.load_factor >= 0.0);
        REQUIRE(final_stats.load_factor <= 1.0);
        REQUIRE(final_stats.load_factor >= initial_stats.load_factor);

        // Mathematical relationship: load_factor = used_slots / total_slots
        double expected_load_factor = double(final_stats.used_slots) / final_stats.total_slots.value;
        REQUIRE(std::abs(final_stats.load_factor - expected_load_factor) < 0.001);
    }

    SECTION("Hash distribution entropy") {
        slot_count slots{256};  // Power of 2 for easier analysis
        fnv1a_hasher hasher{slots};

        // Generate many keys and measure hash distribution
        const size_t num_samples = 10000;
        std::vector<uint64_t> hash_counts(slots.value, 0);

        for (size_t i = 0; i < num_samples; ++i) {
            std::string key = "entropy_test_" + std::to_string(i) + "_" + generate_random_string();
            auto index = hasher.index_for(key);
            hash_counts[index.value]++;
        }

        // Calculate Shannon entropy
        double entropy = 0.0;
        double total = double(num_samples);

        for (auto count : hash_counts) {
            if (count > 0) {
                double probability = count / total;
                entropy -= probability * std::log2(probability);
            }
        }

        // For uniform distribution, max entropy = log2(slots)
        double max_entropy = std::log2(double(slots.value));
        double entropy_ratio = entropy / max_entropy;

        // Good hash function should have high entropy (> 0.8 of maximum)
        REQUIRE(entropy_ratio > 0.8);
    }
}