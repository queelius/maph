/**
 * @file hashers.hpp
 * @brief Hash function implementations - each does one thing well
 */

#pragma once

#include "core.hpp"
#include "phf_concept.hpp"
#include <array>
#include <vector>
#include <memory>
#include <algorithm>
#include <cstring>
#include <span>
#include <unordered_map>

namespace maph {

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
template<typename H>
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
 * Satisfies perfect_hash_function concept. Also provides hash()/max_slots()
 * compatibility methods so it can be used directly with hash_table<>.
 *
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

        builder& add_all(const std::vector<std::string>& keys) {
            for (const auto& k : keys) keys_.push_back(k);
            return *this;
        }

        result<minimal_perfect_hasher> build();
    };

private:
    struct impl {
        std::unordered_map<std::string, slot_index> key_to_slot_;
        slot_count total_slots_;

        impl(size_t num_slots) : total_slots_{num_slots} {}
    };

    std::unique_ptr<impl> pimpl_;

    explicit minimal_perfect_hasher(std::unique_ptr<impl> p) : pimpl_(std::move(p)) {}

public:
    minimal_perfect_hasher() : pimpl_(std::make_unique<impl>(0)) {}

    minimal_perfect_hasher(minimal_perfect_hasher&&) = default;
    minimal_perfect_hasher& operator=(minimal_perfect_hasher&&) = default;

    // --- perfect_hash_function concept interface ---
    [[nodiscard]] slot_index slot_for(std::string_view key) const noexcept;
    [[nodiscard]] size_t num_keys() const noexcept;
    [[nodiscard]] size_t range_size() const noexcept;
    [[nodiscard]] double bits_per_key() const noexcept;
    [[nodiscard]] size_t memory_bytes() const noexcept;

    // --- table-layer compatibility (hash_table needs hash() + max_slots()) ---
    // Offset by 1 so hash is never 0 (slot layer treats hash 0 as empty)
    [[nodiscard]] hash_value hash(std::string_view key) const noexcept {
        return hash_value{slot_for(key).value + 1};
    }
    [[nodiscard]] slot_count max_slots() const noexcept {
        return slot_count{range_size()};
    }

    // Serialization with clear ownership
    [[nodiscard]] std::vector<std::byte> serialize() const;
    [[nodiscard]] static result<minimal_perfect_hasher> deserialize(std::span<const std::byte>);
};

/**
 * @class hybrid_hasher
 * @brief Wraps a perfect hash function for use with hash_table
 *
 * Delegates to the PHF's slot_for() for all keys. The fallback hasher
 * is kept for keys outside the PHF's build set (the PHF returns an
 * arbitrary slot for those, so collisions are handled by the table layer).
 *
 * Provides hash()/max_slots() so it satisfies the hash_table Hasher
 * requirements.
 */
template<typename P, typename H>
class hybrid_hasher {
    P perfect_;
    H fallback_;

public:
    constexpr hybrid_hasher(P perfect, H fallback) noexcept
        : perfect_(std::move(perfect)), fallback_(std::move(fallback)) {}

    [[nodiscard]] hash_value hash(std::string_view key) const noexcept {
        return hash_value{perfect_.slot_for(key).value};
    }

    [[nodiscard]] slot_count max_slots() const noexcept {
        return slot_count{perfect_.range_size()};
    }

    [[nodiscard]] auto resolve(std::string_view key) const noexcept {
        struct resolution {
            hash_value hash_val;
            slot_index index;
        };

        auto idx = perfect_.slot_for(key);
        return resolution{hash_value{idx.value}, idx};
    }
};

// Factory function for easy composition
template<typename P, typename H>
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

    auto p = std::make_unique<minimal_perfect_hasher::impl>(keys_.size());

    // Assign consecutive slots to keys
    for (size_t i = 0; i < keys_.size(); ++i) {
        p->key_to_slot_[keys_[i]] = slot_index{i};
    }

    return minimal_perfect_hasher{std::move(p)};
}

inline slot_index minimal_perfect_hasher::slot_for(std::string_view key) const noexcept {
    if (auto it = pimpl_->key_to_slot_.find(std::string(key));
        it != pimpl_->key_to_slot_.end()) {
        return it->second;
    }
    // Unknown key: return 0 (arbitrary valid index per PHF contract)
    return slot_index{0};
}

inline size_t minimal_perfect_hasher::num_keys() const noexcept {
    return pimpl_->key_to_slot_.size();
}

inline size_t minimal_perfect_hasher::range_size() const noexcept {
    return pimpl_->total_slots_.value;
}

inline double minimal_perfect_hasher::bits_per_key() const noexcept {
    if (pimpl_->key_to_slot_.empty()) return 0.0;
    return static_cast<double>(memory_bytes() * 8) / pimpl_->key_to_slot_.size();
}

inline size_t minimal_perfect_hasher::memory_bytes() const noexcept {
    // Approximate: each entry has a string key + slot_index in the unordered_map
    size_t bytes = sizeof(impl);
    for (const auto& [key, _] : pimpl_->key_to_slot_) {
        bytes += key.size() + sizeof(slot_index) + 64; // overhead per bucket
    }
    return bytes;
}

inline std::vector<std::byte> minimal_perfect_hasher::serialize() const {
    std::vector<std::byte> buf;
    // Serialize slot count
    auto count_bytes = reinterpret_cast<const std::byte*>(&pimpl_->total_slots_.value);
    buf.insert(buf.end(), count_bytes, count_bytes + sizeof(pimpl_->total_slots_.value));

    // Serialize key mappings
    for (const auto& [key, slot] : pimpl_->key_to_slot_) {
        // Key length
        size_t len = key.size();
        auto len_bytes = reinterpret_cast<const std::byte*>(&len);
        buf.insert(buf.end(), len_bytes, len_bytes + sizeof(len));

        // Key data
        auto key_bytes = reinterpret_cast<const std::byte*>(key.data());
        buf.insert(buf.end(), key_bytes, key_bytes + len);

        // Slot index
        auto slot_bytes = reinterpret_cast<const std::byte*>(&slot.value);
        buf.insert(buf.end(), slot_bytes, slot_bytes + sizeof(slot.value));
    }

    return buf;
}

inline result<minimal_perfect_hasher> minimal_perfect_hasher::deserialize(std::span<const std::byte> data) {
    size_t offset = 0;

    auto read_bytes = [&](void* out, size_t n) -> bool {
        if (offset + n > data.size()) return false;
        std::memcpy(out, data.data() + offset, n);
        offset += n;
        return true;
    };

    // Read slot count
    uint64_t total_slots = 0;
    if (!read_bytes(&total_slots, sizeof(total_slots))) {
        return std::unexpected(error::invalid_format);
    }

    auto p = std::make_unique<impl>(static_cast<size_t>(total_slots));

    // Read key mappings until we run out of data
    while (offset < data.size()) {
        // Key length
        size_t len = 0;
        if (!read_bytes(&len, sizeof(len))) break;

        // Bounds check
        if (len > data.size() - offset) {
            return std::unexpected(error::invalid_format);
        }

        // Key data
        std::string key(len, '\0');
        if (!read_bytes(key.data(), len)) {
            return std::unexpected(error::invalid_format);
        }

        // Slot index
        slot_index slot{0};
        if (!read_bytes(&slot.value, sizeof(slot.value))) {
            return std::unexpected(error::invalid_format);
        }

        p->key_to_slot_[std::move(key)] = slot;
    }

    return minimal_perfect_hasher{std::move(p)};
}

} // namespace maph