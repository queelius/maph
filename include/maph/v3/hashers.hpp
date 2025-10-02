/**
 * @file hashers.hpp
 * @brief Hash function implementations - each does one thing well
 */

#pragma once

#include "core.hpp"
#include <array>
#include <vector>
#include <memory>
#include <algorithm>
#include <unordered_map>

namespace maph::v3 {

// ===== STANDARD HASH FUNCTIONS =====

/**
 * @class fnv1a_hasher
 * @brief FNV-1a hash function with configurable slot count
 *
 * Simple, fast, good distribution. Does one thing well.
 */
class fnv1a_hasher {
    slot_count slots_;

public:
    explicit constexpr fnv1a_hasher(slot_count n) noexcept : slots_(n) {}

    [[nodiscard]] hash_value hash(std::string_view key) const noexcept {
        constexpr uint64_t fnv_offset_basis = 14695981039346656037ULL;
        constexpr uint64_t fnv_prime = 1099511628211ULL;

        uint64_t h = fnv_offset_basis;
        for (unsigned char c : key) {
            h ^= c;
            h *= fnv_prime;
        }
        return hash_value{h ? h : 1}; // Never return 0
    }

    [[nodiscard]] constexpr slot_count max_slots() const noexcept {
        return slots_;
    }

    [[nodiscard]] slot_index index_for(std::string_view key) const noexcept {
        return slot_index{hash(key) % slots_.value};
    }
};

/**
 * @class linear_probe_hasher
 * @brief Decorator that adds linear probing to any hasher
 *
 * Composable design - wraps any hasher to add collision resolution
 */
template<hasher H>
class linear_probe_hasher {
    H base_;
    size_t max_probes_;

public:
    constexpr linear_probe_hasher(H base, size_t max_probes = 10) noexcept
        : base_(std::move(base)), max_probes_(max_probes) {}

    [[nodiscard]] hash_value hash(std::string_view key) const noexcept {
        return base_.hash(key);
    }

    [[nodiscard]] constexpr slot_count max_slots() const noexcept {
        return base_.max_slots();
    }

    /**
     * @brief Generate probe sequence for a key
     * @return Generator that yields slot indices
     */
    [[nodiscard]] auto probe_sequence(std::string_view key) const noexcept {
        struct probe_iterator {
            slot_index start;
            slot_count total;
            size_t max_probes;
            size_t current{0};

            slot_index operator*() const noexcept {
                return slot_index{(start.value + current) % total.value};
            }

            probe_iterator& operator++() noexcept {
                ++current;
                return *this;
            }

            bool at_end() const noexcept {
                return current >= max_probes;
            }
        };

        auto h = base_.hash(key);
        auto start = slot_index{h % max_slots().value};
        return probe_iterator{start, max_slots(), max_probes_};
    }
};

// ===== PERFECT HASH FUNCTIONS =====

/**
 * @class minimal_perfect_hasher
 * @brief Minimal perfect hash function with guaranteed O(1) lookups
 *
 * Clean interface for perfect hashing without implementation details leaking
 * This is a simplified implementation for demonstration purposes.
 */
class minimal_perfect_hasher {
public:
    struct builder {
        std::vector<std::string> keys_;

        builder& add(std::string_view key) {
            keys_.emplace_back(key);
            return *this;
        }

        result<minimal_perfect_hasher> build();
    };

private:
    struct impl {
        std::unordered_map<std::string, slot_index> key_to_slot_;
        std::vector<hash_value> slot_hashes_;
        slot_count total_slots_;

        impl(size_t num_slots) : total_slots_{num_slots} {
            slot_hashes_.resize(num_slots, hash_value{0});
        }
    };

    std::unique_ptr<impl> pimpl_;

    explicit minimal_perfect_hasher(std::unique_ptr<impl> p) : pimpl_(std::move(p)) {}

public:
    minimal_perfect_hasher() : pimpl_(std::make_unique<impl>(0)) {}

    minimal_perfect_hasher(minimal_perfect_hasher&&) = default;
    minimal_perfect_hasher& operator=(minimal_perfect_hasher&&) = default;

    [[nodiscard]] hash_value hash(std::string_view key) const noexcept;
    [[nodiscard]] slot_count max_slots() const noexcept;
    [[nodiscard]] bool is_perfect_for(std::string_view key) const noexcept;
    [[nodiscard]] std::optional<slot_index> slot_for(std::string_view key) const noexcept;

    // Serialization with clear ownership
    [[nodiscard]] std::vector<std::byte> serialize() const;
    [[nodiscard]] static result<minimal_perfect_hasher> deserialize(std::span<const std::byte>);
};

/**
 * @class hybrid_hasher
 * @brief Combines perfect and standard hashing elegantly
 *
 * Uses perfect hash for known keys, falls back to standard for others
 */
template<perfect_hasher P, hasher H>
class hybrid_hasher {
    P perfect_;
    H fallback_;

public:
    constexpr hybrid_hasher(P perfect, H fallback) noexcept
        : perfect_(std::move(perfect)), fallback_(std::move(fallback)) {}

    [[nodiscard]] hash_value hash(std::string_view key) const noexcept {
        if (perfect_.is_perfect_for(key)) {
            return perfect_.hash(key);
        }
        return fallback_.hash(key);
    }

    [[nodiscard]] slot_count max_slots() const noexcept {
        return perfect_.max_slots();
    }

    [[nodiscard]] auto resolve(std::string_view key) const noexcept {
        struct resolution {
            hash_value hash;
            slot_index index;
            bool is_perfect;
        };

        if (auto slot = perfect_.slot_for(key)) {
            return resolution{perfect_.hash(key), *slot, true};
        }

        auto h = fallback_.hash(key);
        return resolution{h, slot_index{h % max_slots().value}, false};
    }
};

// Factory function for easy composition
template<perfect_hasher P, hasher H>
auto make_hybrid(P&& p, H&& h) {
    return hybrid_hasher<std::decay_t<P>, std::decay_t<H>>{
        std::forward<P>(p), std::forward<H>(h)
    };
}

// ===== IMPLEMENTATION =====

inline result<minimal_perfect_hasher> minimal_perfect_hasher::builder::build() {
    if (keys_.empty()) {
        return std::unexpected(error::optimization_failed);
    }

    // Sort and deduplicate keys
    std::sort(keys_.begin(), keys_.end());
    keys_.erase(std::unique(keys_.begin(), keys_.end()), keys_.end());

    auto impl = std::make_unique<minimal_perfect_hasher::impl>(keys_.size());

    // Simple FNV-1a hash for demonstration
    auto compute_hash = [](std::string_view key) -> hash_value {
        constexpr uint64_t fnv_offset_basis = 14695981039346656037ULL;
        constexpr uint64_t fnv_prime = 1099511628211ULL;

        uint64_t h = fnv_offset_basis;
        for (unsigned char c : key) {
            h ^= c;
            h *= fnv_prime;
        }
        return hash_value{h ? h : 1};
    };

    // Assign slots to keys
    for (size_t i = 0; i < keys_.size(); ++i) {
        impl->key_to_slot_[keys_[i]] = slot_index{i};
        impl->slot_hashes_[i] = compute_hash(keys_[i]);
    }

    return minimal_perfect_hasher{std::move(impl)};
}

inline hash_value minimal_perfect_hasher::hash(std::string_view key) const noexcept {
    if (auto it = pimpl_->key_to_slot_.find(std::string(key));
        it != pimpl_->key_to_slot_.end()) {
        return pimpl_->slot_hashes_[it->second.value];
    }
    // For unknown keys, return a hash that maps outside the perfect range
    return hash_value{pimpl_->total_slots_.value + 1};
}

inline slot_count minimal_perfect_hasher::max_slots() const noexcept {
    return pimpl_->total_slots_;
}

inline bool minimal_perfect_hasher::is_perfect_for(std::string_view key) const noexcept {
    return pimpl_->key_to_slot_.find(std::string(key)) != pimpl_->key_to_slot_.end();
}

inline std::optional<slot_index> minimal_perfect_hasher::slot_for(std::string_view key) const noexcept {
    if (auto it = pimpl_->key_to_slot_.find(std::string(key));
        it != pimpl_->key_to_slot_.end()) {
        return it->second;
    }
    return std::nullopt;
}

inline std::vector<std::byte> minimal_perfect_hasher::serialize() const {
    // Simplified serialization - in production would use a proper format
    std::vector<std::byte> result;
    // Serialize slot count
    auto count_bytes = reinterpret_cast<const std::byte*>(&pimpl_->total_slots_.value);
    result.insert(result.end(), count_bytes, count_bytes + sizeof(pimpl_->total_slots_.value));

    // Serialize key mappings
    for (const auto& [key, slot] : pimpl_->key_to_slot_) {
        // Key length
        size_t len = key.size();
        auto len_bytes = reinterpret_cast<const std::byte*>(&len);
        result.insert(result.end(), len_bytes, len_bytes + sizeof(len));

        // Key data
        auto key_bytes = reinterpret_cast<const std::byte*>(key.data());
        result.insert(result.end(), key_bytes, key_bytes + len);

        // Slot index
        auto slot_bytes = reinterpret_cast<const std::byte*>(&slot.value);
        result.insert(result.end(), slot_bytes, slot_bytes + sizeof(slot.value));
    }

    return result;
}

inline result<minimal_perfect_hasher> minimal_perfect_hasher::deserialize(std::span<const std::byte> data) {
    // Simplified deserialization
    if (data.size() < sizeof(uint64_t)) {
        return std::unexpected(error::invalid_format);
    }

    // This is a stub - full implementation would parse the serialized format
    return std::unexpected(error::invalid_format);
}

} // namespace maph::v3