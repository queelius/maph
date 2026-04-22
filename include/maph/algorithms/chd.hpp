/**
 * @file chd.hpp
 * @brief CHD (Compress, Hash, and Displace) perfect hash function.
 *
 * CHD is a classic minimal perfect hash function with good performance.
 * It's been extensively tested and is used in many production systems.
 *
 * References:
 * - Belazzougui et al. "Hash, displace, and compress" (2009)
 * - Space: ~2.0-2.5 bits per key
 * - Query time: O(1), typically 30-50ns
 */

#pragma once

#include "../core.hpp"
#include "../concepts/perfect_hash_function.hpp"
#include "../detail/hash.hpp"
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
 * @class chd_hasher
 * @brief CHD (Compress, Hash, and Displace) perfect hash
 */
class chd_hasher {
public:
    class builder;

private:
    std::vector<uint32_t> displacements_;  // Displacement per bucket
    std::vector<int64_t> slot_map_;        // Sparse position -> dense slot (-1 if unused)
    size_t key_count_{0};
    size_t num_buckets_{0};
    size_t table_size_{0};  // Total slots in hash table (larger than key_count_)
    double lambda_{5.0};    // Average bucket size
    uint64_t seed_{0};

    // First hash: determines bucket
    [[nodiscard]] size_t bucket_hash(std::string_view key) const noexcept {
        return phf_hash_with_seed(key, seed_) % num_buckets_;
    }

    // Second hash: determines slot within table given displacement
    [[nodiscard]] size_t slot_hash(std::string_view key, uint32_t displacement) const noexcept {
        return (phf_hash_with_seed(key, seed_ ^ 0xCAFEBABE12345678ULL) + displacement) % table_size_;
    }

    explicit chd_hasher(size_t key_count, double lambda, uint64_t seed)
        : key_count_(key_count)
        , num_buckets_(std::max(size_t{1}, static_cast<size_t>(std::ceil(key_count / lambda))))
        , table_size_(static_cast<size_t>(std::ceil(key_count * 2.0)))  // 2x overhead for reliable displacement finding
        , lambda_(lambda)
        , seed_(seed) {
        displacements_.resize(num_buckets_, 0);
        slot_map_.assign(table_size_, -1);  // Initialize all to unused
    }

public:
    chd_hasher() = default;
    chd_hasher(chd_hasher&&) = default;
    chd_hasher& operator=(chd_hasher&&) = default;

    /**
     * @brief Get slot index for a key (deterministic, no verification)
     * @param key Key to look up
     * @return Slot index in [0, num_keys())
     */
    [[nodiscard]] slot_index slot_for(std::string_view key) const noexcept {
        if (key_count_ == 0) return slot_index{0};

        size_t bucket = bucket_hash(key);
        uint32_t displacement = displacements_[bucket];
        size_t sparse_slot = slot_hash(key, displacement);

        if (sparse_slot < table_size_ && slot_map_[sparse_slot] >= 0) {
            return slot_index{static_cast<uint64_t>(slot_map_[sparse_slot])};
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

    [[nodiscard]] size_t table_size() const noexcept { return table_size_; }

    // Algorithm identifier for serialization
    static constexpr uint32_t ALGORITHM_ID = 2;  // CHD

    [[nodiscard]] std::vector<std::byte> serialize() const {
        std::vector<std::byte> out;
        phf_serial::write_header(out, ALGORITHM_ID);

        phf_serial::append(out, static_cast<uint64_t>(key_count_));
        phf_serial::append(out, static_cast<uint64_t>(num_buckets_));
        phf_serial::append(out, static_cast<uint64_t>(table_size_));
        phf_serial::append(out, lambda_);
        phf_serial::append(out, seed_);

        phf_serial::append_vector(out, displacements_);
        phf_serial::append_vector(out, slot_map_);
        return out;
    }

    [[nodiscard]] static result<chd_hasher> deserialize(std::span<const std::byte> data) {
        phf_serial::reader r(data);

        if (!phf_serial::verify_header(r, ALGORITHM_ID)) {
            return std::unexpected(error::invalid_format);
        }

        uint64_t key_count_u64{}, num_buckets_u64{}, table_size_u64{}, seed{};
        double lambda{};
        if (!r.read(key_count_u64) || !r.read(num_buckets_u64) ||
            !r.read(table_size_u64) || !r.read(lambda) || !r.read(seed)) {
            return std::unexpected(error::invalid_format);
        }
        if (key_count_u64 > MAX_SERIALIZED_ELEMENT_COUNT) {
            return std::unexpected(error::invalid_format);
        }

        chd_hasher hasher(static_cast<size_t>(key_count_u64), lambda, seed);
        hasher.num_buckets_ = static_cast<size_t>(num_buckets_u64);
        hasher.table_size_ = static_cast<size_t>(table_size_u64);

        if (!r.read_vector(hasher.displacements_) || !r.read_vector(hasher.slot_map_)) {
            return std::unexpected(error::invalid_format);
        }
        return hasher;
    }

    class builder {
        std::vector<std::string> keys_;
        double lambda_{5.0};
        uint64_t seed_{0x123456789abcdef0ULL};

    public:
        builder() = default;

        builder& add(std::string_view key) {
            keys_.emplace_back(key);
            return *this;
        }

        builder& add_all(const std::vector<std::string>& keys) {
            keys_.insert(keys_.end(), keys.begin(), keys.end());
            return *this;
        }

        builder& with_lambda(double l) {
            lambda_ = std::max(1.0, l);
            return *this;
        }

        builder& with_seed(uint64_t seed) {
            seed_ = seed;
            return *this;
        }

        [[nodiscard]] result<chd_hasher> build() {
            if (keys_.empty()) {
                return std::unexpected(error::optimization_failed);
            }

            // Remove duplicates
            std::sort(keys_.begin(), keys_.end());
            keys_.erase(std::unique(keys_.begin(), keys_.end()), keys_.end());

            for (int attempt = 0; attempt < 50; ++attempt) {
                uint64_t attempt_seed = seed_ ^ (attempt * 0x9e3779b97f4a7c15ULL);

                chd_hasher hasher(keys_.size(), lambda_, attempt_seed);

                // Group keys by bucket
                std::vector<std::vector<size_t>> bucket_keys(hasher.num_buckets_);
                for (size_t i = 0; i < keys_.size(); ++i) {
                    size_t bucket = hasher.bucket_hash(keys_[i]);
                    bucket_keys[bucket].push_back(i);
                }

                // Sort buckets by size (largest first) for better displacement finding
                std::vector<size_t> bucket_order(hasher.num_buckets_);
                std::iota(bucket_order.begin(), bucket_order.end(), 0);
                std::sort(bucket_order.begin(), bucket_order.end(),
                    [&bucket_keys](size_t a, size_t b) {
                        return bucket_keys[a].size() > bucket_keys[b].size();
                    });

                // Track which slots are used
                std::vector<bool> used_slots(hasher.table_size_, false);
                size_t next_dense_slot = 0;
                bool all_placed = true;

                // Find displacement for each bucket
                for (size_t bucket_idx : bucket_order) {
                    const auto& keys_in_bucket = bucket_keys[bucket_idx];
                    if (keys_in_bucket.empty()) {
                        hasher.displacements_[bucket_idx] = 0;
                        continue;
                    }

                    // Find a displacement that gives no collisions
                    bool found = false;
                    for (uint32_t d = 0; d < 65535; ++d) {
                        std::vector<size_t> tentative_slots;
                        bool collision = false;

                        for (size_t key_idx : keys_in_bucket) {
                            size_t slot = hasher.slot_hash(keys_[key_idx], d);
                            if (used_slots[slot]) {
                                collision = true;
                                break;
                            }
                            // Check for collision within this bucket
                            for (size_t s : tentative_slots) {
                                if (s == slot) {
                                    collision = true;
                                    break;
                                }
                            }
                            if (collision) break;
                            tentative_slots.push_back(slot);
                        }

                        if (!collision) {
                            // Found valid displacement
                            hasher.displacements_[bucket_idx] = d;
                            for (size_t i = 0; i < keys_in_bucket.size(); ++i) {
                                size_t sparse_slot = tentative_slots[i];
                                used_slots[sparse_slot] = true;
                                // Map sparse slot to dense slot
                                hasher.slot_map_[sparse_slot] = static_cast<int64_t>(next_dense_slot);
                                ++next_dense_slot;
                            }
                            found = true;
                            break;
                        }
                    }

                    if (!found) {
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
 * @brief Create a CHD hasher from keys
 */
[[nodiscard]] inline result<chd_hasher>
make_chd(std::span<const std::string> keys, double lambda = 5.0, uint64_t seed = 0) {
    return chd_hasher::builder{}
        .add_all(std::vector<std::string>(keys.begin(), keys.end()))
        .with_lambda(lambda)
        .with_seed(seed)
        .build();
}

// ===== STATIC ASSERTIONS =====

static_assert(perfect_hash_function<chd_hasher>);

} // namespace maph
