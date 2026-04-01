/**
 * @file core.hpp
 * @brief Core types and concepts for maph v3
 *
 * This file defines the fundamental building blocks for a composable
 * perfect hash table implementation. Each component does one thing well.
 */

#pragma once

#include <concepts>
#include <expected>
#include <span>
#include <string_view>
#include <cstdint>
#include <memory>
#include <functional>
#include <optional>
#include <atomic>
#include <algorithm>

namespace maph {

// ===== STRONG TYPES =====
// Replace primitive obsession with intention-revealing types

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
// Use std::expected for elegant error propagation

enum class error {
    success,
    io_error,
    invalid_format,
    key_not_found,
    table_full,
    value_too_large,
    permission_denied,
    optimization_failed
};

template<typename T>
using result = std::expected<T, error>;

using status = std::expected<void, error>;

// ===== CORE CONCEPTS =====
// Define clear interfaces using concepts

/**
 * @concept storage_backend
 * @brief A type that provides storage for slots
 */
template<typename S>
concept storage_backend = requires(S s, slot_index idx, hash_value hash, std::span<const std::byte> data) {
    { s.read(idx) };
    { s.write(idx, hash, data) };
    { s.clear(idx) };
    { s.get_slot_count() };
    { s.empty(idx) };
    { s.hash_at(idx) };
};

// ===== VALUE SEMANTICS =====
// Immutable value type for keys

class key {
    std::string_view data_;
public:
    explicit constexpr key(std::string_view sv) noexcept : data_(sv) {}
    constexpr std::string_view view() const noexcept { return data_; }
    constexpr auto operator<=>(const key&) const = default;
};

// Immutable value type for values
class value {
    std::span<const std::byte> data_;
public:
    constexpr value() noexcept : data_{} {}
    explicit constexpr value(std::span<const std::byte> bytes) noexcept : data_(bytes) {}
    constexpr std::span<const std::byte> bytes() const noexcept { return data_; }
    constexpr size_t size() const noexcept { return data_.size(); }
};

// ===== SLOT ABSTRACTION =====
// Clean abstraction for a single slot

template<size_t Size = 512>
class slot {
public:
    static constexpr size_t slot_size = Size;
    static constexpr size_t metadata_size = 16;
    static constexpr size_t data_size = Size - metadata_size;

private:
    struct metadata {
        std::atomic<uint64_t> hash_version{0};
        uint32_t size{0};
        uint32_t reserved{0};

        metadata() = default;
        metadata(const metadata& other)
            : hash_version(other.hash_version.load()),
              size(other.size),
              reserved(other.reserved) {}
        metadata& operator=(const metadata& other) {
            hash_version = other.hash_version.load();
            size = other.size;
            reserved = other.reserved;
            return *this;
        }
    };

    alignas(64) metadata meta_;
    std::byte data_[data_size];

public:
    slot() = default;
    slot(const slot& other) : meta_(other.meta_) {
        std::copy(std::begin(other.data_), std::end(other.data_), std::begin(data_));
    }
    slot& operator=(const slot& other) {
        if (this != &other) {
            meta_ = other.meta_;
            std::copy(std::begin(other.data_), std::end(other.data_), std::begin(data_));
        }
        return *this;
    }
    slot(slot&&) = default;
    slot& operator=(slot&&) = default;

    constexpr bool empty() const noexcept {
        return (meta_.hash_version.load() >> 32) == 0;
    }

    result<value> get() const noexcept {
        if (empty()) {
            return std::unexpected(error::key_not_found);
        }
        return value{std::span{data_, meta_.size}};
    }

    status set(hash_value hash, std::span<const std::byte> data) noexcept {
        if (data.size() > data_size) {
            return std::unexpected(error::value_too_large);
        }

        // Store only lower 32 bits of hash in upper 32 bits of hash_version
        auto hash_32 = static_cast<uint32_t>(hash.value & 0xFFFFFFFF);
        auto version = static_cast<uint32_t>(meta_.hash_version.load());
        meta_.hash_version.store((uint64_t(hash_32) << 32) | (version + 1));
        meta_.size = static_cast<uint32_t>(data.size());
        std::copy(data.begin(), data.end(), data_);
        meta_.hash_version.store((uint64_t(hash_32) << 32) | (version + 2));

        return {};
    }

    void clear() noexcept {
        auto version = static_cast<uint32_t>(meta_.hash_version.load());
        meta_.hash_version.store(version + 2);
        meta_.size = 0;
    }

    hash_value hash() const noexcept {
        // Return 32-bit hash from upper 32 bits, zero-extended to 64 bits
        auto hash_32 = static_cast<uint32_t>(meta_.hash_version.load() >> 32);
        return hash_value{hash_32};
    }
};

static_assert(sizeof(slot<512>) == 512);

} // namespace maph