/**
 * @file test_core.cpp
 * @brief Tests for strong types and error handling in core.hpp
 */

#include <catch2/catch_test_macros.hpp>
#include <maph/core.hpp>
#include <type_traits>

using namespace maph;

// ===== STRONG TYPES =====

TEST_CASE("slot_index default-constructs to zero", "[core][strong_types]") {
    slot_index s;
    REQUIRE(s.value == 0);
}

TEST_CASE("slot_index wraps uint64_t explicitly", "[core][strong_types]") {
    slot_index s{42};
    REQUIRE(s.value == 42);
    REQUIRE(static_cast<uint64_t>(s) == 42);
}

TEST_CASE("slot_index constructor is explicit", "[core][strong_types]") {
    STATIC_REQUIRE_FALSE(std::is_convertible_v<uint64_t, slot_index>);
    STATIC_REQUIRE(std::is_constructible_v<slot_index, uint64_t>);
}

TEST_CASE("hash_value wraps uint64_t explicitly", "[core][strong_types]") {
    hash_value h{0xdeadbeef};
    REQUIRE(h.value == 0xdeadbeef);
    REQUIRE(static_cast<uint64_t>(h) == 0xdeadbeef);
    STATIC_REQUIRE_FALSE(std::is_convertible_v<uint64_t, hash_value>);
}

TEST_CASE("slot_count wraps uint64_t explicitly", "[core][strong_types]") {
    slot_count c{1024};
    REQUIRE(c.value == 1024);
    REQUIRE(static_cast<uint64_t>(c) == 1024);
    STATIC_REQUIRE_FALSE(std::is_convertible_v<uint64_t, slot_count>);
}

TEST_CASE("strong types do not implicitly convert to each other", "[core][strong_types]") {
    // Explicit construction from uint64_t is allowed, but implicit
    // cross-type assignment (slot_index x = hash_value{...}) is not.
    STATIC_REQUIRE_FALSE(std::is_convertible_v<hash_value, slot_index>);
    STATIC_REQUIRE_FALSE(std::is_convertible_v<slot_index, hash_value>);
    STATIC_REQUIRE_FALSE(std::is_convertible_v<slot_index, slot_count>);
}

// ===== ERROR HANDLING =====

TEST_CASE("result<T> carries success values", "[core][error_handling]") {
    result<int> r = 42;
    REQUIRE(r.has_value());
    REQUIRE(*r == 42);
}

TEST_CASE("result<T> carries errors via unexpected", "[core][error_handling]") {
    result<int> r = std::unexpected(error::key_not_found);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error() == error::key_not_found);
}

TEST_CASE("error enum values are distinct", "[core][error_handling]") {
    REQUIRE(error::success != error::io_error);
    REQUIRE(error::io_error != error::invalid_format);
    REQUIRE(error::key_not_found != error::value_too_large);
    REQUIRE(error::permission_denied != error::optimization_failed);
}
