/**
 * @file core.hpp
 * @brief Core types for maph: strong types and error handling.
 *
 * Foundational types shared by the perfect_hash_function concept, the
 * algorithm implementations, and the perfect_filter composition layer.
 */

#pragma once

#include <expected>
#include <cstdint>

namespace maph {

// ===== STRONG TYPES =====
// Replace primitive obsession with intention-revealing types.

struct slot_index {
    uint64_t value;
    constexpr slot_index() : value(0) {}
    explicit constexpr slot_index(uint64_t v) : value(v) {}
    constexpr operator uint64_t() const { return value; }
};

struct hash_value {
    uint64_t value;
    explicit constexpr hash_value(uint64_t v) : value(v) {}
    constexpr operator uint64_t() const { return value; }
};

struct slot_count {
    uint64_t value;
    explicit constexpr slot_count(uint64_t v) : value(v) {}
    constexpr operator uint64_t() const { return value; }
};

// ===== ERROR HANDLING =====
// std::expected (C++23) for elegant error propagation.

enum class error {
    success,
    io_error,
    invalid_format,
    key_not_found,
    value_too_large,
    permission_denied,
    optimization_failed
};

template<typename T>
using result = std::expected<T, error>;

} // namespace maph
