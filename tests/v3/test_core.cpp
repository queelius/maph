/**
 * @file test_core.cpp
 * @brief Comprehensive tests for maph core types and concepts
 *
 * Tests focus on behavior contracts rather than implementation details.
 * These tests should remain valid even if the internal implementation changes.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_adapters.hpp>
#include <catch2/generators/catch_generators_random.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>

#include <maph/core.hpp>
#include <type_traits>
#include <thread>
#include <vector>
#include <string_view>

using namespace maph;

// ===== STRONG TYPES TESTS =====
// Test that strong types behave correctly and prevent primitive obsession

TEST_CASE("slot_index behavior", "[core][strong_types]") {
    SECTION("Construction and conversion") {
        slot_index idx{42};
        REQUIRE(idx.value == 42);
        REQUIRE(static_cast<uint64_t>(idx) == 42);
    }

    SECTION("Explicit construction prevents accidents") {
        // This should not compile - we want explicit construction
        // slot_index idx = 42;  // Would be compilation error

        // This should work - explicit construction
        slot_index idx{42};
        REQUIRE(idx.value == 42);
    }

    SECTION("Value semantics") {
        slot_index idx1{10};
        slot_index idx2{10};
        slot_index idx3{20};

        REQUIRE(idx1.value == idx2.value);
        REQUIRE(idx1.value != idx3.value);
    }
}

TEST_CASE("hash_value behavior", "[core][strong_types]") {
    SECTION("Non-zero hash values") {
        hash_value h1{1};
        hash_value h2{0};  // Zero is allowed but unusual

        REQUIRE(h1.value == 1);
        REQUIRE(h2.value == 0);
    }

    SECTION("Large hash values") {
        hash_value h{UINT64_MAX};
        REQUIRE(h.value == UINT64_MAX);
    }
}

TEST_CASE("slot_count behavior", "[core][strong_types]") {
    SECTION("Reasonable slot counts") {
        slot_count small{100};
        slot_count large{1000000};

        REQUIRE(small.value == 100);
        REQUIRE(large.value == 1000000);
    }

    SECTION("Zero slot count edge case") {
        slot_count zero{0};
        REQUIRE(zero.value == 0);
    }
}

// ===== ERROR HANDLING TESTS =====
// Test the error handling system with std::expected

TEST_CASE("Error types and result handling", "[core][error_handling]") {
    SECTION("Success results") {
        result<int> success{42};

        REQUIRE(success.has_value());
        REQUIRE(success.value() == 42);
        REQUIRE_FALSE(!success);
    }

    SECTION("Error results") {
        result<int> failure{std::unexpected(error::key_not_found)};

        REQUIRE_FALSE(failure.has_value());
        REQUIRE(failure.error() == error::key_not_found);
        REQUIRE(!failure);
    }

    SECTION("Status type for void operations") {
        status success{};
        status failure{std::unexpected(error::io_error)};

        REQUIRE(success.has_value());
        REQUIRE_FALSE(failure.has_value());
        REQUIRE(failure.error() == error::io_error);
    }

    SECTION("Error propagation patterns") {
        auto create_error = []() -> result<int> {
            return std::unexpected(error::table_full);
        };

        auto chain_operation = [&]() -> result<int> {
            auto r = create_error();
            if (!r) return std::unexpected(r.error());
            return r.value() * 2;
        };

        auto final_result = chain_operation();
        REQUIRE_FALSE(final_result.has_value());
        REQUIRE(final_result.error() == error::table_full);
    }
}

TEST_CASE("Error type completeness", "[core][error_handling]") {
    SECTION("All error types are represented") {
        std::vector<error> all_errors = {
            error::success,
            error::io_error,
            error::invalid_format,
            error::key_not_found,
            error::table_full,
            error::value_too_large,
            error::permission_denied,
            error::optimization_failed
        };

        // Each error should be distinct
        for (size_t i = 0; i < all_errors.size(); ++i) {
            for (size_t j = i + 1; j < all_errors.size(); ++j) {
                REQUIRE(all_errors[i] != all_errors[j]);
            }
        }
    }
}

// ===== VALUE TYPES TESTS =====
// Test immutable value semantics for keys and values

TEST_CASE("key type behavior", "[core][value_types]") {
    SECTION("Construction from string_view") {
        std::string str = "test_key";
        std::string_view sv = str;
        key k{sv};

        REQUIRE(k.view() == sv);
        REQUIRE(k.view().data() == sv.data());  // Should be same pointer
    }

    SECTION("Comparison operations") {
        key k1{"abc"};
        key k2{"abc"};
        key k3{"def"};

        REQUIRE(k1 == k2);
        REQUIRE(k1 != k3);
        REQUIRE(k1 < k3);
    }

    SECTION("Immutability") {
        key k{"test"};
        auto view = k.view();

        // The key should maintain its view even if we get it multiple times
        REQUIRE(k.view() == view);
        REQUIRE(k.view().data() == view.data());
    }
}

TEST_CASE("value type behavior", "[core][value_types]") {
    SECTION("Construction from byte span") {
        std::string str = "test_value";
        auto bytes = std::span{reinterpret_cast<const std::byte*>(str.data()), str.size()};
        value v{bytes};

        REQUIRE(v.bytes().data() == bytes.data());
        REQUIRE(v.size() == str.size());
    }

    SECTION("Empty value") {
        value empty{std::span<const std::byte>{}};
        REQUIRE(empty.size() == 0);
        REQUIRE(empty.bytes().empty());
    }

    SECTION("Large value") {
        std::vector<std::byte> large_data(10000, std::byte{0x42});
        value large{large_data};

        REQUIRE(large.size() == 10000);
        REQUIRE(large.bytes().size() == 10000);
    }
}

// ===== SLOT ABSTRACTION TESTS =====
// Test the slot abstraction with focus on thread safety and atomicity

TEST_CASE("slot basic operations", "[core][slot]") {
    slot<512> s;

    SECTION("Initial state") {
        REQUIRE(s.empty());
        REQUIRE_FALSE(s.get().has_value());
        REQUIRE(s.get().error() == error::key_not_found);
    }

    SECTION("Set and get operations") {
        std::string test_data = "test_data_123";
        auto bytes = std::span{reinterpret_cast<const std::byte*>(test_data.data()), test_data.size()};
        hash_value h{12345};

        auto set_result = s.set(h, bytes);
        REQUIRE(set_result.has_value());
        REQUIRE_FALSE(s.empty());
        REQUIRE(s.hash() == h);

        auto get_result = s.get();
        REQUIRE(get_result.has_value());
        REQUIRE(get_result->size() == test_data.size());

        // Convert back to string for comparison
        auto retrieved = get_result->bytes();
        std::string_view retrieved_str{reinterpret_cast<const char*>(retrieved.data()), retrieved.size()};
        REQUIRE(retrieved_str == test_data);
    }

    SECTION("Clear operation") {
        std::string data = "data";
        auto bytes = std::span{reinterpret_cast<const std::byte*>(data.data()), data.size()};

        s.set(hash_value{123}, bytes);
        REQUIRE_FALSE(s.empty());

        s.clear();
        REQUIRE(s.empty());
        REQUIRE_FALSE(s.get().has_value());
    }

    SECTION("Value too large") {
        // Try to store more data than the slot can hold
        std::vector<std::byte> large_data(slot<512>::data_size + 1, std::byte{0x42});

        auto result = s.set(hash_value{123}, large_data);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == error::value_too_large);
        REQUIRE(s.empty());
    }

    SECTION("Maximum size data") {
        // Store exactly the maximum amount of data
        std::vector<std::byte> max_data(slot<512>::data_size, std::byte{0x42});

        auto result = s.set(hash_value{123}, max_data);
        REQUIRE(result.has_value());
        REQUIRE_FALSE(s.empty());

        auto retrieved = s.get();
        REQUIRE(retrieved.has_value());
        REQUIRE(retrieved->size() == max_data.size());
    }
}

TEST_CASE("slot thread safety", "[core][slot][threading]") {
    slot<512> s;
    constexpr size_t num_threads = 10;
    constexpr size_t operations_per_thread = 100;

    SECTION("Concurrent writes don't corrupt data") {
        std::vector<std::thread> writers;
        std::atomic<size_t> success_count{0};

        for (size_t t = 0; t < num_threads; ++t) {
            writers.emplace_back([&, t]() {
                for (size_t i = 0; i < operations_per_thread; ++i) {
                    std::string data = "thread_" + std::to_string(t) + "_op_" + std::to_string(i);
                    auto bytes = std::span{reinterpret_cast<const std::byte*>(data.data()), data.size()};

                    // Use different hash values to avoid intentional conflicts
                    hash_value h{t * operations_per_thread + i + 1};

                    if (s.set(h, bytes)) {
                        success_count++;
                    }
                }
            });
        }

        for (auto& writer : writers) {
            writer.join();
        }

        // At least one write should have succeeded
        REQUIRE(success_count > 0);

        // If slot is not empty, we should be able to read valid data
        if (!s.empty()) {
            auto result = s.get();
            REQUIRE(result.has_value());
            REQUIRE(result->size() > 0);
        }
    }

    SECTION("Concurrent reads are safe") {
        // First, write some data
        std::string test_data = "concurrent_read_test";
        auto bytes = std::span{reinterpret_cast<const std::byte*>(test_data.data()), test_data.size()};
        s.set(hash_value{999}, bytes);

        std::vector<std::thread> readers;
        std::atomic<size_t> successful_reads{0};

        for (size_t t = 0; t < num_threads; ++t) {
            readers.emplace_back([&]() {
                for (size_t i = 0; i < operations_per_thread; ++i) {
                    auto result = s.get();
                    if (result.has_value()) {
                        successful_reads++;

                        // Verify data integrity
                        auto retrieved = result->bytes();
                        std::string_view retrieved_str{reinterpret_cast<const char*>(retrieved.data()), retrieved.size()};
                        REQUIRE(retrieved_str == test_data);
                    }
                }
            });
        }

        for (auto& reader : readers) {
            reader.join();
        }

        REQUIRE(successful_reads == num_threads * operations_per_thread);
    }
}

TEST_CASE("slot size template parameter", "[core][slot]") {
    SECTION("Different slot sizes compile correctly") {
        slot<256> small_slot;
        slot<1024> large_slot;
        slot<4096> huge_slot;

        REQUIRE(small_slot.slot_size == 256);
        REQUIRE(large_slot.slot_size == 1024);
        REQUIRE(huge_slot.slot_size == 4096);

        REQUIRE(small_slot.data_size == 256 - 16);
        REQUIRE(large_slot.data_size == 1024 - 16);
        REQUIRE(huge_slot.data_size == 4096 - 16);
    }

    SECTION("Static assertions hold") {
        // The static assertion in the header should ensure slot<512> is exactly 512 bytes
        REQUIRE(sizeof(slot<512>) == 512);
    }
}

// ===== CONCEPT TESTS =====
// Test that concepts work correctly and provide good error messages

// Mock types for concept testing
struct mock_hasher {
    hash_value hash(std::string_view) const { return hash_value{1}; }
    slot_count max_slots() const { return slot_count{100}; }
};

struct mock_storage {
    result<std::span<const std::byte>> read(slot_index) const {
        return std::unexpected(error::key_not_found);
    }
    status write(slot_index, hash_value, std::span<const std::byte>) {
        return {};
    }
    status clear(slot_index) {
        return {};
    }
    maph::slot_count get_slot_count() const {
        return maph::slot_count{100};
    }
    bool empty(slot_index) const {
        return true;
    }
    hash_value hash_at(slot_index) const {
        return hash_value{0};
    }
};

struct mock_perfect_hasher {
    hash_value hash(std::string_view) const { return hash_value{1}; }
    slot_count max_slots() const { return slot_count{100}; }
    bool is_perfect_for(std::string_view) const { return true; }
    std::optional<slot_index> slot_for(std::string_view) const {
        return slot_index{0};
    }
};

TEST_CASE("Concept validation", "[core][concepts]") {
    SECTION("hasher concept") {
        REQUIRE(hasher<mock_hasher>);

        // Test that non-conforming types are rejected
        REQUIRE_FALSE(hasher<int>);
        REQUIRE_FALSE(hasher<std::string>);
    }

    SECTION("storage_backend concept") {
        REQUIRE(storage_backend<mock_storage>);

        // Test that non-conforming types are rejected
        REQUIRE_FALSE(storage_backend<int>);
        REQUIRE_FALSE(storage_backend<mock_hasher>);
    }

    SECTION("perfect_hasher concept") {
        REQUIRE(perfect_hasher<mock_perfect_hasher>);
        REQUIRE(hasher<mock_perfect_hasher>);  // Should also satisfy hasher

        // Test that regular hashers don't satisfy perfect_hasher
        REQUIRE_FALSE(perfect_hasher<mock_hasher>);
    }
}

// ===== PROPERTY-BASED TESTS FOR CORE TYPES =====
// Test invariants that should always hold

TEST_CASE("Core type invariants", "[core][properties]") {
    SECTION("slot_index arithmetic properties") {
        // Test with multiple values
        for (uint64_t test_value : {0ULL, 1ULL, 42ULL, 100ULL, 10000ULL}) {
            slot_index idx{test_value};
            REQUIRE(idx.value == test_value);
            REQUIRE(static_cast<uint64_t>(idx) == test_value);

            // Identity property
            REQUIRE(slot_index{static_cast<uint64_t>(idx)}.value == idx.value);
        }
    }

    SECTION("hash_value properties") {
        // Test with multiple values
        std::vector<uint64_t> test_values = {1ULL, 42ULL, 1000ULL, UINT64_MAX};
        for (uint64_t test_value : test_values) {
            hash_value h{test_value};
            REQUIRE(h.value == test_value);
            REQUIRE(static_cast<uint64_t>(h) == test_value);
        }
    }

    SECTION("Key comparison transitivity") {
        // Test that key comparisons are transitive: if a < b and b < c, then a < c
        std::vector<std::string> test_strings = {"a", "ab", "abc", "b", "bc", "c"};

        for (size_t i = 0; i < test_strings.size(); ++i) {
            for (size_t j = 0; j < test_strings.size(); ++j) {
                for (size_t k = 0; k < test_strings.size(); ++k) {
                    key a{test_strings[i]};
                    key b{test_strings[j]};
                    key c{test_strings[k]};

                    // Transitivity: if a < b and b < c, then a < c
                    if (a < b && b < c) {
                        REQUIRE(a < c);
                    }

                    // Reflexivity: a == a
                    REQUIRE(a == key{test_strings[i]});

                    // Symmetry: if a == b, then b == a
                    if (a == b) {
                        REQUIRE(b == a);
                    }
                }
            }
        }
    }
}

// ===== EDGE CASE TESTS =====
// Test boundary conditions and unusual inputs

TEST_CASE("Edge cases and boundary conditions", "[core][edge_cases]") {
    SECTION("Empty key") {
        key empty_key{""};
        REQUIRE(empty_key.view().empty());
        REQUIRE(empty_key.view().size() == 0);
    }

    SECTION("Very long key") {
        std::string long_string(10000, 'x');
        key long_key{long_string};
        REQUIRE(long_key.view().size() == 10000);
        REQUIRE(long_key.view() == long_string);
    }

    SECTION("Keys with special characters") {
        std::vector<std::string> special_keys = {
            "\0\0\0",  // Null characters
            "\xFF\xFE\xFD",  // High byte values
            "key\nwith\nnewlines",  // Control characters
            "key\twith\ttabs",
            "key with spaces",
            "key/with/slashes",
            "key\\with\\backslashes"
        };

        for (const auto& test_key : special_keys) {
            key k{test_key};
            REQUIRE(k.view() == test_key);

            // Keys should be comparable even with special characters
            key k2{test_key};
            REQUIRE(k == k2);
        }
    }

    SECTION("Slot versioning under rapid updates") {
        slot<512> s;
        hash_value h{123};

        // Rapidly update the slot many times
        for (size_t i = 0; i < 1000; ++i) {
            std::string data = "data_" + std::to_string(i);
            auto bytes = std::span{reinterpret_cast<const std::byte*>(data.data()), data.size()};

            REQUIRE(s.set(h, bytes).has_value());

            auto result = s.get();
            REQUIRE(result.has_value());

            // Verify we can read back what we wrote
            auto retrieved = result->bytes();
            std::string_view retrieved_str{reinterpret_cast<const char*>(retrieved.data()), retrieved.size()};
            REQUIRE(retrieved_str == data);
        }
    }
}