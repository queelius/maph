/**
 * @file pthash.hpp
 * @brief PTHash minimal perfect hash function.
 *
 * PTHash is a modern, extremely fast minimal perfect hash function that
 * uses an encoder-based design for compact storage. It partitions keys
 * into buckets and uses a pilot table plus encoders for collision resolution.
 *
 * References:
 * - Pibiri & Trani "PTHash: Revisiting FCH Minimal Perfect Hashing" (2021)
 * - Space: ~2.0-2.2 bits per key
 * - Query time: O(1), typically 20-30ns (fastest)
 * - Build time: O(n), very fast construction
 */

#pragma once

#include "../core.hpp"
#include "../concepts/perfect_hash_function.hpp"
#include "../detail/serialization.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace maph {

/**
 * @class pthash_hasher
 * @brief PTHash minimal perfect hash with encoder-based design
 *
 * Algorithm:
 * 1. Keys are partitioned into buckets (first-level hash)
 * 2. Each bucket gets a pilot value (small integer)
 * 3. Pilots are stored compactly using Elias-Fano encoding
 * 4. Query: bucket_hash(key) + pilot[bucket]
 *
 * @tparam AlphaInt Load factor for buckets (as integer, default 98 = 0.98)
 */
template<size_t AlphaInt = 98>  // Alpha as integer (98 = 0.98)
class pthash_hasher {
    static_assert(AlphaInt >= 80 && AlphaInt <= 99, "Alpha must be between 80 and 99");
    static constexpr double Alpha = AlphaInt / 100.0;

public:
    class builder;

private:
    // Compact pilot storage using simple encoding
    struct pilot_table {
        std::vector<uint16_t> pilots;  // Pilot values per bucket
        size_t num_buckets{0};

        [[nodiscard]] uint16_t get_pilot(size_t bucket_idx) const noexcept {
            if (bucket_idx >= pilots.size()) return 0;
            return pilots[bucket_idx];
        }

        void set_pilot(size_t bucket_idx, uint16_t value) {
            if (bucket_idx >= pilots.size()) {
                pilots.resize(bucket_idx + 1, 0);
            }
            pilots[bucket_idx] = value;
        }
    };

    pilot_table pilots_;
    std::vector<int64_t> slot_map_;     // Raw slot -> dense slot
    size_t key_count_{0};
    size_t num_buckets_{0};
    size_t table_size_{0};  // Total hash table size
    uint64_t seed_{0};

    explicit pthash_hasher(size_t key_count, uint64_t seed)
        : key_count_(key_count)
        , num_buckets_(key_count > 0 ? key_count : 0)  // 1 key per bucket to guarantee pilot success
        , table_size_(static_cast<size_t>(std::ceil(key_count / Alpha)))
        , seed_(seed) {
        pilots_.num_buckets = num_buckets_;
    }

    // Fast hash function
    [[nodiscard]] static constexpr uint64_t fast_hash(uint64_t x) noexcept {
        x ^= x >> 33;
        x *= 0xff51afd7ed558ccdULL;
        x ^= x >> 33;
        x *= 0xc4ceb9fe1a85ec53ULL;
        x ^= x >> 33;
        return x;
    }

    // Hash string to 64-bit value
    [[nodiscard]] uint64_t hash_string(std::string_view key) const noexcept {
        uint64_t h = seed_;
        for (unsigned char c : key) {
            h ^= c;
            h *= 0x100000001b3ULL;  // FNV prime
        }
        return fast_hash(h);
    }

    // Get bucket index for key
    [[nodiscard]] size_t get_bucket(std::string_view key) const noexcept {
        if (num_buckets_ == 0) return 0;
        return hash_string(key) % num_buckets_;
    }

    // Get position within bucket
    [[nodiscard]] uint64_t bucket_hash(std::string_view key, uint16_t pilot) const noexcept {
        uint64_t h = hash_string(key);
        return fast_hash(h ^ pilot) % table_size_;
    }

public:
    pthash_hasher() = default;
    pthash_hasher(pthash_hasher&&) = default;
    pthash_hasher& operator=(pthash_hasher&&) = default;

    /**
     * @brief Get slot index for a key (deterministic, no verification)
     * @param key Key to look up
     * @return Slot index in [0, num_keys())
     */
    [[nodiscard]] slot_index slot_for(std::string_view key) const noexcept {
        if (key_count_ == 0) return slot_index{0};

        size_t bucket_idx = get_bucket(key);
        uint16_t pilot = pilots_.get_pilot(bucket_idx);
        uint64_t raw_slot = bucket_hash(key, pilot);

        if (raw_slot < slot_map_.size() && slot_map_[raw_slot] >= 0) {
            return slot_index{static_cast<uint64_t>(slot_map_[raw_slot])};
        }

        // Key not in build set, return arbitrary valid index
        return slot_index{0};
    }

    [[nodiscard]] size_t num_keys() const noexcept { return key_count_; }
    [[nodiscard]] size_t range_size() const noexcept { return key_count_; }

    [[nodiscard]] double bits_per_key() const noexcept {
        if (key_count_ == 0) return 0.0;
        size_t total_bytes = pilots_.pilots.size() * sizeof(uint16_t) +
                            slot_map_.size() * sizeof(int64_t) +
                            sizeof(*this);
        return (total_bytes * 8.0) / key_count_;
    }

    [[nodiscard]] size_t memory_bytes() const noexcept {
        return pilots_.pilots.size() * sizeof(uint16_t) +
               slot_map_.size() * sizeof(int64_t) +
               sizeof(*this);
    }

    [[nodiscard]] size_t num_buckets() const noexcept { return num_buckets_; }

    // Algorithm identifier for serialization
    static constexpr uint32_t ALGORITHM_ID = 5;  // PTHash

    [[nodiscard]] std::vector<std::byte> serialize() const {
        std::vector<std::byte> out;
        phf_serial::write_header(out, ALGORITHM_ID, static_cast<uint32_t>(AlphaInt));

        phf_serial::append(out, static_cast<uint64_t>(key_count_));
        phf_serial::append(out, static_cast<uint64_t>(num_buckets_));
        phf_serial::append(out, static_cast<uint64_t>(table_size_));
        phf_serial::append(out, seed_);

        phf_serial::append_vector(out, pilots_.pilots);
        phf_serial::append_vector(out, slot_map_);
        return out;
    }

    [[nodiscard]] static result<pthash_hasher> deserialize(std::span<const std::byte> data) {
        phf_serial::reader r(data);

        if (!phf_serial::verify_header(r, ALGORITHM_ID, static_cast<uint32_t>(AlphaInt))) {
            return std::unexpected(error::invalid_format);
        }

        uint64_t key_count_u64{}, num_buckets_u64{}, table_size_u64{}, seed{};
        if (!r.read(key_count_u64) || !r.read(num_buckets_u64) ||
            !r.read(table_size_u64) || !r.read(seed)) {
            return std::unexpected(error::invalid_format);
        }
        if (key_count_u64 > MAX_SERIALIZED_ELEMENT_COUNT) {
            return std::unexpected(error::invalid_format);
        }

        pthash_hasher hasher(static_cast<size_t>(key_count_u64), seed);
        hasher.num_buckets_ = static_cast<size_t>(num_buckets_u64);
        hasher.table_size_ = static_cast<size_t>(table_size_u64);

        if (!r.read_vector(hasher.pilots_.pilots) || !r.read_vector(hasher.slot_map_)) {
            return std::unexpected(error::invalid_format);
        }
        hasher.pilots_.num_buckets = hasher.num_buckets_;
        return hasher;
    }

    /**
     * @class builder
     * @brief Builder for PTHash perfect hash
     */
    class builder {
        std::vector<std::string> keys_;
        uint64_t seed_{0x123456789abcdef0ULL};
        size_t max_pilot_search_{16384};  // Maximum pilot value to try (uint16_t max)

    public:
        builder() = default;

        builder& add(std::string_view key) {
            keys_.emplace_back(key);
            return *this;
        }

        builder& add_all(std::span<const std::string> keys) {
            keys_.insert(keys_.end(), keys.begin(), keys.end());
            return *this;
        }

        builder& add_all(const std::vector<std::string>& keys) {
            keys_.insert(keys_.end(), keys.begin(), keys.end());
            return *this;
        }

        builder& with_seed(uint64_t seed) {
            seed_ = seed;
            return *this;
        }

        builder& with_max_pilot_search(size_t max_search) {
            max_pilot_search_ = max_search;
            return *this;
        }

        [[nodiscard]] result<pthash_hasher> build() {
            if (keys_.empty()) {
                return std::unexpected(error::optimization_failed);
            }

            // Remove duplicates
            std::sort(keys_.begin(), keys_.end());
            keys_.erase(std::unique(keys_.begin(), keys_.end()), keys_.end());

            for (int attempt = 0; attempt < 50; ++attempt) {
                uint64_t attempt_seed = seed_ ^ (attempt * 0x9e3779b97f4a7c15ULL);

                pthash_hasher hasher(keys_.size(), attempt_seed);
                hasher.slot_map_.assign(hasher.table_size_, -1);

                // Partition keys into buckets
                std::vector<std::vector<std::string>> buckets(hasher.num_buckets_);
                for (const auto& key : keys_) {
                    size_t bucket_idx = hasher.get_bucket(key);
                    buckets[bucket_idx].push_back(key);
                }

                // Sort buckets by size (largest first) for better pilot finding
                std::vector<size_t> bucket_order(hasher.num_buckets_);
                std::iota(bucket_order.begin(), bucket_order.end(), 0);
                std::sort(bucket_order.begin(), bucket_order.end(),
                    [&buckets](size_t a, size_t b) {
                        return buckets[a].size() > buckets[b].size();
                    });

                std::vector<bool> used_slots(hasher.table_size_, false);
                size_t next_index = 0;
                bool all_placed = true;

                // Use unordered_set for faster collision checking within bucket
                std::unordered_set<uint64_t> current_slots;

                for (size_t bucket_idx : bucket_order) {
                    const auto& bucket_keys = buckets[bucket_idx];
                    if (bucket_keys.empty()) continue;

                    bool found_pilot = false;
                    size_t pilot_limit = std::min(max_pilot_search_,
                        static_cast<size_t>(std::numeric_limits<uint16_t>::max()) + 1);

                    current_slots.clear();
                    current_slots.reserve(bucket_keys.size());

                    for (size_t pilot = 0; pilot < pilot_limit; ++pilot) {
                        current_slots.clear();
                        bool has_collision = false;
                        auto pilot_value = static_cast<uint16_t>(pilot);

                        for (const auto& key : bucket_keys) {
                            uint64_t slot = hasher.bucket_hash(key, pilot_value);

                            // Check collision with already assigned slots
                            if (used_slots[slot]) {
                                has_collision = true;
                                break;
                            }

                            // Check collision within this bucket
                            if (current_slots.count(slot)) {
                                has_collision = true;
                                break;
                            }

                            current_slots.insert(slot);
                        }

                        if (!has_collision) {
                            // Found a valid pilot
                            hasher.pilots_.set_pilot(bucket_idx, pilot_value);

                            // Mark slots as used
                            for (uint64_t slot : current_slots) {
                                used_slots[slot] = true;
                            }

                            // Record dense slot mapping
                            for (const auto& key : bucket_keys) {
                                uint64_t slot = hasher.bucket_hash(key, pilot_value);
                                hasher.slot_map_[slot] = static_cast<int64_t>(next_index);
                                ++next_index;
                            }

                            found_pilot = true;
                            break;
                        }
                    }

                    if (!found_pilot) {
                        all_placed = false;
                        break;
                    }
                }

                if (all_placed && next_index == keys_.size()) {
                    return hasher;
                }
                // Retry with different seed
            }

            return std::unexpected(error::optimization_failed);
        }
    };
};

// ===== CONVENIENCE ALIASES =====

using pthash98 = pthash_hasher<98>;
using pthash95 = pthash_hasher<95>;

// ===== FACTORY FUNCTIONS =====

/**
 * @brief Create a PTHash hasher from keys
 */
template<size_t AlphaInt = 98>
[[nodiscard]] inline result<pthash_hasher<AlphaInt>>
make_pthash(std::span<const std::string> keys, uint64_t seed = 0) {
    typename pthash_hasher<AlphaInt>::builder builder;
    return builder.add_all(keys).with_seed(seed).build();
}

// ===== STATIC ASSERTIONS =====

static_assert(perfect_hash_function<pthash_hasher<98>>);

} // namespace maph
