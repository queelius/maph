/**
 * @file fch.hpp
 * @brief FCH (Fox, Chazelle, Heath) minimal perfect hash function.
 *
 * FCH is a simple and elegant minimal perfect hash function that's easy
 * to understand and implement. It uses a two-level hashing scheme with
 * buckets and displacement values. Despite its simplicity, it achieves
 * competitive space usage and query performance.
 *
 * References:
 * - Fox et al. "A Practical Minimal Perfect Hashing Method" (1992)
 * - Space: ~2.0-3.0 bits per key
 * - Query time: O(1), typically 25-40ns
 * - Build time: O(n), fast construction
 */

#pragma once

#include "../core.hpp"
#include "../concepts/perfect_hash_function.hpp"
#include "../detail/serialization.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace maph {

/**
 * @class fch_hasher
 * @brief FCH (Fox, Chazelle, Heath) minimal perfect hash
 *
 * Algorithm:
 * 1. Hash keys into buckets using primary hash
 * 2. Sort buckets by size (descending)
 * 3. For each bucket, find displacement value that avoids collisions
 * 4. Store displacement array (compact integer array)
 * 5. Query: secondary_hash(key) + displacement[bucket(key)]
 */
class fch_hasher {
public:
    class builder;

private:
    std::vector<uint32_t> displacements_;  // Displacement per bucket
    std::vector<int64_t> slot_map_;        // Raw table position -> dense slot (-1 if empty)
    size_t key_count_{0};
    size_t num_buckets_{0};
    size_t table_size_{0};
    double bucket_size_{4.0};  // Average keys per bucket
    uint64_t seed_{0};

    explicit fch_hasher(size_t key_count, double bucket_size, uint64_t seed)
        : key_count_(key_count)
        , num_buckets_(key_count > 0 ? std::max(size_t{1},
            static_cast<size_t>(std::ceil(key_count / bucket_size))) : 0)
        , table_size_(static_cast<size_t>(std::ceil(key_count * 3.0)))  // 3x overhead for reliable displacement finding
        , bucket_size_(bucket_size)
        , seed_(seed) {
        displacements_.resize(num_buckets_, 0);
    }

    // Primary hash: assign key to bucket
    [[nodiscard]] uint64_t hash1(std::string_view key) const noexcept {
        uint64_t h = seed_;
        for (unsigned char c : key) {
            h = h * 31 + c;
        }
        return h;
    }

    // Secondary hash: position within table
    [[nodiscard]] uint64_t hash2(std::string_view key) const noexcept {
        uint64_t h = seed_ ^ 0x9e3779b97f4a7c15ULL;
        for (unsigned char c : key) {
            h ^= c;
            h *= 0x100000001b3ULL;  // FNV prime
        }
        // MurmurHash3 finalizer
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccdULL;
        h ^= h >> 33;
        h *= 0xc4ceb9fe1a85ec53ULL;
        h ^= h >> 33;
        return h;
    }

    [[nodiscard]] size_t get_bucket(std::string_view key) const noexcept {
        if (num_buckets_ == 0) return 0;
        return hash1(key) % num_buckets_;
    }

    [[nodiscard]] uint64_t get_position(std::string_view key, uint32_t displacement) const noexcept {
        if (table_size_ == 0) return 0;
        return (hash2(key) + displacement) % table_size_;
    }

public:
    fch_hasher() = default;
    fch_hasher(fch_hasher&&) = default;
    fch_hasher& operator=(fch_hasher&&) = default;

    /**
     * @brief Get slot index for a key (deterministic, no verification)
     * @param key Key to look up
     * @return Slot index in [0, num_keys())
     */
    [[nodiscard]] slot_index slot_for(std::string_view key) const noexcept {
        if (key_count_ == 0) return slot_index{0};

        if (num_buckets_ > 0 && table_size_ > 0 && !slot_map_.empty()) {
            size_t bucket_idx = get_bucket(key);
            uint32_t displacement = displacements_[bucket_idx];
            uint64_t raw_position = get_position(key, displacement);

            if (raw_position < slot_map_.size() && slot_map_[raw_position] >= 0) {
                return slot_index{static_cast<uint64_t>(slot_map_[raw_position])};
            }
        }

        // Key not in build set, return arbitrary valid index
        return slot_index{0};
    }

    [[nodiscard]] size_t num_keys() const noexcept { return key_count_; }
    [[nodiscard]] size_t range_size() const noexcept { return key_count_; }

    [[nodiscard]] double bits_per_key() const noexcept {
        if (key_count_ == 0) return 0.0;
        size_t total_bytes = displacements_.size() * sizeof(uint32_t) +
                            slot_map_.size() * sizeof(int64_t) +
                            sizeof(*this);
        return (total_bytes * 8.0) / key_count_;
    }

    [[nodiscard]] size_t memory_bytes() const noexcept {
        return displacements_.size() * sizeof(uint32_t) +
               slot_map_.size() * sizeof(int64_t) +
               sizeof(*this);
    }

    [[nodiscard]] size_t num_buckets() const noexcept { return num_buckets_; }

    // Algorithm identifier for serialization
    static constexpr uint32_t ALGORITHM_ID = 4;  // FCH

    [[nodiscard]] std::vector<std::byte> serialize() const {
        std::vector<std::byte> out;
        phf_serial::write_header(out, ALGORITHM_ID);

        phf_serial::append(out, static_cast<uint64_t>(key_count_));
        phf_serial::append(out, static_cast<uint64_t>(num_buckets_));
        phf_serial::append(out, static_cast<uint64_t>(table_size_));
        phf_serial::append(out, bucket_size_);
        phf_serial::append(out, seed_);

        phf_serial::append_vector(out, displacements_);
        phf_serial::append_vector(out, slot_map_);
        return out;
    }

    [[nodiscard]] static result<fch_hasher> deserialize(std::span<const std::byte> data) {
        phf_serial::reader r(data);

        if (!phf_serial::verify_header(r, ALGORITHM_ID)) {
            return std::unexpected(error::invalid_format);
        }

        uint64_t key_count_u64{}, num_buckets_u64{}, table_size_u64{}, seed{};
        double bucket_size{};
        if (!r.read(key_count_u64) || !r.read(num_buckets_u64) ||
            !r.read(table_size_u64) || !r.read(bucket_size) || !r.read(seed)) {
            return std::unexpected(error::invalid_format);
        }
        if (key_count_u64 > MAX_SERIALIZED_ELEMENT_COUNT) {
            return std::unexpected(error::invalid_format);
        }

        fch_hasher hasher(static_cast<size_t>(key_count_u64), bucket_size, seed);
        hasher.num_buckets_ = static_cast<size_t>(num_buckets_u64);
        hasher.table_size_ = static_cast<size_t>(table_size_u64);

        if (!r.read_vector(hasher.displacements_) || !r.read_vector(hasher.slot_map_)) {
            return std::unexpected(error::invalid_format);
        }
        return hasher;
    }

    /**
     * @class builder
     * @brief Builder for FCH perfect hash
     */
    class builder {
        std::vector<std::string> keys_;
        double bucket_size_{4.0};  // Average keys per bucket (smaller = more buckets = faster)
        uint64_t seed_{0x123456789abcdef0ULL};
        size_t max_displacement_search_{100000};  // Maximum displacement to try

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

        builder& with_bucket_size(double size) {
            if (size < 1.0) size = 1.0;
            if (size > 100.0) size = 100.0;
            bucket_size_ = size;
            return *this;
        }

        builder& with_seed(uint64_t seed) {
            seed_ = seed;
            return *this;
        }

        builder& with_max_displacement_search(size_t max_search) {
            max_displacement_search_ = std::max(size_t{100}, max_search);
            return *this;
        }

        [[nodiscard]] result<fch_hasher> build() {
            if (keys_.empty()) {
                return std::unexpected(error::optimization_failed);
            }

            // Remove duplicates
            std::sort(keys_.begin(), keys_.end());
            keys_.erase(std::unique(keys_.begin(), keys_.end()), keys_.end());

            for (int attempt = 0; attempt < 50; ++attempt) {
                uint64_t attempt_seed = seed_ ^ (attempt * 0x9e3779b97f4a7c15ULL);

                fch_hasher hasher(keys_.size(), bucket_size_, attempt_seed);
                hasher.slot_map_.assign(hasher.table_size_, -1);

                // Step 1: Partition keys into buckets
                std::vector<std::vector<std::string>> buckets(hasher.num_buckets_);
                for (const auto& key : keys_) {
                    size_t bucket_idx = hasher.get_bucket(key);
                    buckets[bucket_idx].push_back(key);
                }

                // Step 2: Sort buckets by size (largest first)
                std::vector<size_t> bucket_order(hasher.num_buckets_);
                std::iota(bucket_order.begin(), bucket_order.end(), 0);
                std::sort(bucket_order.begin(), bucket_order.end(),
                    [&buckets](size_t a, size_t b) {
                        return buckets[a].size() > buckets[b].size();
                    });

                // Step 3: For each bucket, find a displacement that avoids collisions
                std::vector<bool> used_positions(hasher.table_size_, false);
                size_t next_dense_slot = 0;
                bool all_placed = true;

                for (size_t bucket_idx : bucket_order) {
                    const auto& bucket_keys = buckets[bucket_idx];
                    if (bucket_keys.empty()) continue;

                    bool found_displacement = false;

                    for (uint32_t displacement = 0; displacement < max_displacement_search_; ++displacement) {
                        std::vector<uint64_t> positions;
                        positions.reserve(bucket_keys.size());
                        bool collision = false;

                        for (const auto& key : bucket_keys) {
                            uint64_t pos = hasher.get_position(key, displacement);

                            if (used_positions[pos]) {
                                collision = true;
                                break;
                            }

                            if (std::find(positions.begin(), positions.end(), pos) != positions.end()) {
                                collision = true;
                                break;
                            }

                            positions.push_back(pos);
                        }

                        if (!collision) {
                            hasher.displacements_[bucket_idx] = displacement;

                            for (size_t i = 0; i < bucket_keys.size(); ++i) {
                                uint64_t pos = positions[i];
                                used_positions[pos] = true;
                                hasher.slot_map_[pos] = static_cast<int64_t>(next_dense_slot);
                                ++next_dense_slot;
                            }

                            found_displacement = true;
                            break;
                        }
                    }

                    if (!found_displacement) {
                        all_placed = false;
                        break;
                    }
                }

                if (all_placed && next_dense_slot == keys_.size()) {
                    return hasher;
                }
                // Retry with different seed
            }

            return std::unexpected(error::optimization_failed);
        }
    };
};

// ===== FACTORY FUNCTIONS =====

/**
 * @brief Create an FCH hasher from keys
 */
[[nodiscard]] inline result<fch_hasher>
make_fch(std::span<const std::string> keys, double bucket_size = 4.0, uint64_t seed = 0) {
    return fch_hasher::builder{}
        .add_all(std::vector<std::string>(keys.begin(), keys.end()))
        .with_bucket_size(bucket_size)
        .with_seed(seed)
        .build();
}

// ===== STATIC ASSERTIONS =====

static_assert(perfect_hash_function<fch_hasher>);

} // namespace maph
