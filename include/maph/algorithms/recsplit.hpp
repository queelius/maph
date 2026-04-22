/**
 * @file recsplit.hpp
 * @brief RecSplit minimal perfect hash function.
 *
 * RecSplit is one of the fastest and most space-efficient minimal perfect
 * hash functions. It works by recursively splitting the key space.
 *
 * References:
 * - Esposito et al. "RecSplit: Minimal Perfect Hashing via Recursive Splitting" (2019)
 * - Space: ~1.8-2.0 bits per key
 * - Query time: O(1), typically 20-40ns
 */

#pragma once

#include "../core.hpp"
#include "../concepts/perfect_hash_function.hpp"
#include "../detail/hash.hpp"
#include "../detail/serialization.hpp"
#include <algorithm>
#include <atomic>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

namespace maph {

/**
 * @class recsplit_hasher
 * @brief RecSplit minimal perfect hash function
 *
 * @tparam LeafSize Size of leaves in the recursion tree (4-16, default 8)
 */
template<size_t LeafSize = 8>
class recsplit_hasher {
    static_assert(LeafSize >= 4 && LeafSize <= 16, "LeafSize must be between 4 and 16");

public:
    class builder;

private:
    // Simplified RecSplit: store a seed (split value) per bucket
    struct bucket {
        uint16_t split{0};  // Split value for this bucket
        size_t num_keys{0};  // Number of keys in bucket
    };

    std::vector<bucket> buckets_;
    std::vector<uint64_t> bucket_offsets_;  // Cumulative slot offsets
    size_t key_count_{0};
    size_t num_buckets_{0};
    uint64_t base_seed_{0};

    // Construction time data (cleared after build)
    struct build_data {
        std::vector<std::string> keys;
        std::mt19937_64 rng;
    };

    explicit recsplit_hasher(size_t key_count, uint64_t seed = 0x123456789abcdef0ULL)
        : key_count_(key_count)
        , num_buckets_(std::max(size_t{1}, (key_count * 4) / LeafSize))  // 4x buckets for better distribution with large sets
        , base_seed_(seed) {
        buckets_.resize(num_buckets_);
        bucket_offsets_.resize(num_buckets_ + 1, 0);
    }

    // Determine which bucket a key belongs to
    [[nodiscard]] size_t bucket_for_key(std::string_view key) const noexcept {
        return phf_hash_with_seed(key, base_seed_) % num_buckets_;
    }

    // Hash within a bucket using the bucket's split value
    [[nodiscard]] size_t slot_in_bucket(std::string_view key, size_t bucket_idx) const noexcept {
        if (buckets_[bucket_idx].num_keys == 0) return 0;

        uint64_t split = buckets_[bucket_idx].split;
        uint64_t bucket_seed = base_seed_ ^ (bucket_idx * 0x9e3779b97f4a7c15ULL) ^ (split * 0xbf58476d1ce4e5b9ULL);
        return phf_hash_with_seed(key, bucket_seed) % buckets_[bucket_idx].num_keys;
    }

public:
    recsplit_hasher() = default;
    recsplit_hasher(recsplit_hasher&&) = default;
    recsplit_hasher& operator=(recsplit_hasher&&) = default;

    /**
     * @brief Get slot index for a key (deterministic, no verification)
     * @param key Key to look up
     * @return Slot index in [0, num_keys())
     */
    [[nodiscard]] slot_index slot_for(std::string_view key) const noexcept {
        if (key_count_ == 0) return slot_index{0};

        size_t bucket_idx = bucket_for_key(key);
        if (buckets_[bucket_idx].num_keys == 0) return slot_index{0};

        size_t local_slot = slot_in_bucket(key, bucket_idx);
        size_t global_slot = bucket_offsets_[bucket_idx] + local_slot;
        return slot_index{global_slot};
    }

    [[nodiscard]] size_t num_keys() const noexcept { return key_count_; }
    [[nodiscard]] size_t range_size() const noexcept { return key_count_; }

    [[nodiscard]] double bits_per_key() const noexcept {
        if (key_count_ == 0) return 0.0;
        size_t bucket_bytes = buckets_.size() * sizeof(bucket);
        size_t offset_bytes = bucket_offsets_.size() * sizeof(uint64_t);
        size_t total_bytes = bucket_bytes + offset_bytes + sizeof(*this);
        return (total_bytes * 8.0) / key_count_;
    }

    [[nodiscard]] size_t memory_bytes() const noexcept {
        size_t bucket_bytes = buckets_.size() * sizeof(bucket);
        size_t offset_bytes = bucket_offsets_.size() * sizeof(uint64_t);
        return bucket_bytes + offset_bytes + sizeof(*this);
    }

    // Algorithm identifier for serialization
    static constexpr uint32_t ALGORITHM_ID = 1;  // RecSplit

    // Serialization
    [[nodiscard]] std::vector<std::byte> serialize() const {
        std::vector<std::byte> out;
        phf_serial::write_header(out, ALGORITHM_ID, static_cast<uint32_t>(LeafSize));

        phf_serial::append(out, static_cast<uint64_t>(key_count_));
        phf_serial::append(out, static_cast<uint64_t>(num_buckets_));
        phf_serial::append(out, base_seed_);

        phf_serial::append(out, static_cast<uint64_t>(buckets_.size()));
        for (const auto& b : buckets_) {
            phf_serial::append(out, b.split);
            phf_serial::append(out, static_cast<uint64_t>(b.num_keys));
        }

        phf_serial::append_vector(out, bucket_offsets_);
        return out;
    }

    [[nodiscard]] static result<recsplit_hasher> deserialize(std::span<const std::byte> data) {
        phf_serial::reader r(data);

        if (!phf_serial::verify_header(r, ALGORITHM_ID, static_cast<uint32_t>(LeafSize))) {
            return std::unexpected(error::invalid_format);
        }

        uint64_t key_count_u64{}, num_buckets_u64{}, base_seed{};
        if (!r.read(key_count_u64) || !r.read(num_buckets_u64) || !r.read(base_seed)) {
            return std::unexpected(error::invalid_format);
        }
        if (key_count_u64 > MAX_SERIALIZED_ELEMENT_COUNT) {
            return std::unexpected(error::invalid_format);
        }

        recsplit_hasher hasher(static_cast<size_t>(key_count_u64), base_seed);
        hasher.num_buckets_ = static_cast<size_t>(num_buckets_u64);

        uint64_t bucket_count{};
        if (!r.read(bucket_count) || bucket_count > MAX_SERIALIZED_ELEMENT_COUNT) {
            return std::unexpected(error::invalid_format);
        }
        hasher.buckets_.resize(static_cast<size_t>(bucket_count));
        for (auto& b : hasher.buckets_) {
            uint64_t num_keys{};
            if (!r.read(b.split) || !r.read(num_keys)) {
                return std::unexpected(error::invalid_format);
            }
            b.num_keys = static_cast<size_t>(num_keys);
        }

        if (!r.read_vector(hasher.bucket_offsets_)) {
            return std::unexpected(error::invalid_format);
        }
        return hasher;
    }

    /**
     * @class builder
     * @brief Builder for RecSplit perfect hash
     */
    class builder {
        std::vector<std::string> keys_;
        uint64_t seed_{0x123456789abcdef0ULL};
        size_t num_threads_{1};

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

        builder& with_threads(size_t threads) {
            num_threads_ = std::max(size_t{1}, threads);
            return *this;
        }

    private:
        // Result of processing a single bucket
        struct bucket_result {
            uint16_t split{0};
            size_t num_keys{0};
            bool success{false};
        };

        // Process a single bucket - can be called in parallel
        [[nodiscard]] bucket_result process_bucket(
            const recsplit_hasher& hasher,
            const std::vector<std::string>& keys_in_bucket,
            size_t bucket_idx) const {

            bucket_result result;

            if (keys_in_bucket.empty()) {
                result.success = true;
                return result;
            }

            // Find a split value that gives no collisions within this bucket
            constexpr uint16_t max_split_search = 10000;

            for (uint16_t split = 0; split < max_split_search; ++split) {
                // Check if all keys get unique slots with this split
                std::unordered_set<size_t> used_slots;
                bool has_collision = false;

                // Temporarily set split to compute slot positions
                auto test_slot_in_bucket = [&](std::string_view key) -> size_t {
                    if (keys_in_bucket.size() == 0) return 0;
                    uint64_t bucket_seed = hasher.base_seed_ ^ (bucket_idx * 0x9e3779b97f4a7c15ULL) ^ (split * 0xbf58476d1ce4e5b9ULL);
                    return phf_hash_with_seed(key, bucket_seed) % keys_in_bucket.size();
                };

                for (const auto& key : keys_in_bucket) {
                    size_t slot = test_slot_in_bucket(key);
                    if (used_slots.count(slot)) {
                        has_collision = true;
                        break;
                    }
                    used_slots.insert(slot);
                }

                if (!has_collision) {
                    result.split = split;
                    result.num_keys = keys_in_bucket.size();
                    result.success = true;
                    return result;
                }
            }

            // Couldn't find split for this bucket
            result.success = false;
            return result;
        }

    public:
        [[nodiscard]] result<recsplit_hasher> build() {
            if (keys_.empty()) {
                return std::unexpected(error::optimization_failed);
            }

            // Remove duplicates
            std::sort(keys_.begin(), keys_.end());
            keys_.erase(std::unique(keys_.begin(), keys_.end()), keys_.end());

            for (int attempt = 0; attempt < 50; ++attempt) {
                uint64_t attempt_seed = seed_ ^ (attempt * 0x9e3779b97f4a7c15ULL);

                recsplit_hasher hasher(keys_.size(), attempt_seed);

                // 1. Partition keys into buckets
                std::vector<std::vector<std::string>> bucket_keys(hasher.num_buckets_);
                for (const auto& key : keys_) {
                    size_t bucket_idx = hasher.bucket_for_key(key);
                    bucket_keys[bucket_idx].push_back(key);
                }

                // 2. Process buckets (potentially in parallel)
                std::vector<bucket_result> results(hasher.num_buckets_);
                bool all_success = true;

                if (num_threads_ > 1 && hasher.num_buckets_ > 100) {
                    // Parallel processing using std::thread
                    std::atomic<bool> any_failure{false};
                    auto process_range = [&](size_t start, size_t end) {
                        for (size_t i = start; i < end; ++i) {
                            results[i] = process_bucket(hasher, bucket_keys[i], i);
                            if (!results[i].success) {
                                any_failure.store(true, std::memory_order_relaxed);
                            }
                        }
                    };

                    std::vector<std::thread> threads;
                    size_t buckets_per_thread = (hasher.num_buckets_ + num_threads_ - 1) / num_threads_;

                    for (size_t t = 0; t < num_threads_; ++t) {
                        size_t start = t * buckets_per_thread;
                        size_t end = std::min(start + buckets_per_thread, hasher.num_buckets_);
                        if (start < end) {
                            threads.emplace_back(process_range, start, end);
                        }
                    }

                    for (auto& thread : threads) {
                        thread.join();
                    }

                    all_success = !any_failure.load();
                } else {
                    // Single-threaded processing
                    for (size_t bucket_idx = 0; bucket_idx < hasher.num_buckets_; ++bucket_idx) {
                        results[bucket_idx] = process_bucket(hasher, bucket_keys[bucket_idx], bucket_idx);
                        if (!results[bucket_idx].success) {
                            all_success = false;
                            break;
                        }
                    }
                }

                if (!all_success) continue;  // Retry with different seed

                // 3. Compute cumulative offsets (sequential, but O(num_buckets))
                size_t cumulative_offset = 0;
                for (size_t bucket_idx = 0; bucket_idx < hasher.num_buckets_; ++bucket_idx) {
                    hasher.bucket_offsets_[bucket_idx] = cumulative_offset;
                    hasher.buckets_[bucket_idx].split = results[bucket_idx].split;
                    hasher.buckets_[bucket_idx].num_keys = results[bucket_idx].num_keys;
                    cumulative_offset += results[bucket_idx].num_keys;
                }
                hasher.bucket_offsets_[hasher.num_buckets_] = cumulative_offset;

                return hasher;
            }

            return std::unexpected(error::optimization_failed);
        }
    };
};

// ===== CONVENIENCE ALIASES =====

using recsplit8 = recsplit_hasher<8>;
using recsplit16 = recsplit_hasher<16>;

// ===== FACTORY FUNCTIONS =====

/**
 * @brief Create a RecSplit hasher from keys
 */
template<size_t LeafSize = 8>
[[nodiscard]] inline result<recsplit_hasher<LeafSize>>
make_recsplit(std::span<const std::string> keys, uint64_t seed = 0) {
    typename recsplit_hasher<LeafSize>::builder builder;
    return builder.add_all(keys).with_seed(seed).build();
}

// ===== STATIC ASSERTIONS =====

static_assert(perfect_hash_function<recsplit_hasher<8>>);

} // namespace maph
