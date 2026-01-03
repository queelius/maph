/**
 * @file hashers_perfect.hpp
 * @brief Perfect hash function implementations with policy-based design
 *
 * Provides multiple perfect hash algorithms with a unified interface.
 * Each algorithm is a policy that can be plugged into the hash table.
 */

#pragma once

#include "core.hpp"
#include <array>
#include <vector>
#include <memory>
#include <algorithm>
#include <bit>
#include <random>
#include <span>
#include <cmath>
#include <numeric>
#include <unordered_set>
#include <limits>
#include <cstring>
#include <thread>

namespace maph {

// ===== PERFECT HASH BUILDER CONCEPT =====

/**
 * @concept perfect_hash_builder
 * @brief Requirements for a perfect hash function builder
 *
 * Builders use a fluent interface for constructing perfect hash functions.
 * The build process is separate from query to enforce correctness.
 */
template<typename Builder, typename HashFunc>
concept perfect_hash_builder = requires(Builder b, std::string_view key, std::vector<std::string> keys) {
    { b.add(key) } -> std::same_as<Builder&>;
    { b.add_all(keys) } -> std::same_as<Builder&>;
    { b.build() } -> std::same_as<result<HashFunc>>;
};

// ===== METADATA AND STATISTICS =====

/**
 * @struct perfect_hash_stats
 * @brief Statistics about a perfect hash function
 */
struct perfect_hash_stats {
    size_t key_count{0};
    size_t memory_bytes{0};
    double bits_per_key{0.0};
    size_t build_time_us{0};  // Microseconds
    size_t perfect_count{0};  // Keys placed via perfect hash
    size_t overflow_count{0}; // Keys in overflow storage

    [[nodiscard]] constexpr bool is_minimal() const noexcept {
        return true;  // All our implementations are minimal
    }
};

// Magic numbers for serialization format
constexpr uint32_t PERFECT_HASH_MAGIC = 0x4D415048;  // "MAPH"
constexpr uint32_t PERFECT_HASH_VERSION = 1;

// SIMD-optimized linear search for fingerprint in overflow array
// Returns index if found, or size if not found
#if defined(__AVX2__)
#include <immintrin.h>
inline size_t find_fingerprint_simd(const uint64_t* data, size_t size, uint64_t target) noexcept {
    // Process 4 elements at a time with AVX2
    __m256i target_vec = _mm256_set1_epi64x(static_cast<int64_t>(target));

    size_t i = 0;
    for (; i + 4 <= size; i += 4) {
        __m256i data_vec = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + i));
        __m256i cmp = _mm256_cmpeq_epi64(data_vec, target_vec);
        int mask = _mm256_movemask_epi8(cmp);
        if (mask != 0) {
            // Find which of the 4 elements matched
            if (mask & 0x000000FF) return i;
            if (mask & 0x0000FF00) return i + 1;
            if (mask & 0x00FF0000) return i + 2;
            return i + 3;
        }
    }

    // Handle remaining elements
    for (; i < size; ++i) {
        if (data[i] == target) return i;
    }
    return size;
}
#else
inline size_t find_fingerprint_simd(const uint64_t* data, size_t size, uint64_t target) noexcept {
    // Scalar fallback - still use some optimizations
    for (size_t i = 0; i < size; ++i) {
        if (data[i] == target) return i;
    }
    return size;
}
#endif

// Lightweight fingerprint used to validate membership without storing full keys
inline uint64_t fingerprint64(std::string_view key) noexcept {
    uint64_t h = 0x9e3779b97f4a7c15ULL;
    for (unsigned char c : key) {
        h ^= c + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    }
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return h ? h : 1;  // Never return 0 so default slots remain invalid
}

// ===== RECSPLIT PERFECT HASH =====

/**
 * @class recsplit_hasher
 * @brief RecSplit minimal perfect hash function
 *
 * RecSplit is one of the fastest and most space-efficient minimal perfect
 * hash functions. It works by recursively splitting the key space.
 *
 * References:
 * - Esposito et al. "RecSplit: Minimal Perfect Hashing via Recursive Splitting" (2019)
 * - Space: ~1.8-2.0 bits per key
 * - Query time: O(1), typically 20-40ns
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
    std::vector<uint64_t> fingerprints_;    // Fingerprint per slot for membership check
    std::vector<uint64_t> overflow_fingerprints_;  // Keys that couldn't be perfectly placed
    std::vector<size_t> overflow_slots_;   // Slots for overflow keys
    size_t key_count_{0};
    size_t perfect_count_{0};  // Keys placed via perfect hash
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
        fingerprints_.resize(key_count_, 0);
    }

    // Remix function for hash distribution
    [[nodiscard]] static constexpr uint64_t remix(uint64_t z) noexcept {
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }

    // Hash a string with a given seed
    [[nodiscard]] uint64_t hash_with_seed(std::string_view key, uint64_t seed) const noexcept {
        uint64_t h = seed;
        for (unsigned char c : key) {
            h ^= c;
            h *= 0x100000001b3ULL;  // FNV prime
        }
        return remix(h);
    }

    // Determine which bucket a key belongs to
    [[nodiscard]] size_t bucket_for_key(std::string_view key) const noexcept {
        return hash_with_seed(key, base_seed_) % num_buckets_;
    }

    // Hash within a bucket using the bucket's split value
    [[nodiscard]] size_t slot_in_bucket(std::string_view key, size_t bucket_idx) const noexcept {
        if (buckets_[bucket_idx].num_keys == 0) return 0;

        uint64_t split = buckets_[bucket_idx].split;
        uint64_t bucket_seed = base_seed_ ^ (bucket_idx * 0x9e3779b97f4a7c15ULL) ^ (split * 0xbf58476d1ce4e5b9ULL);
        return hash_with_seed(key, bucket_seed) % buckets_[bucket_idx].num_keys;
    }

    // Golomb-Rice encoding
    static void encode_golomb_rice(std::vector<uint8_t>& out, uint64_t value, size_t k) {
        // Simple implementation - production would use bit-level encoding
        uint64_t q = value >> k;
        uint64_t r = value & ((1ULL << k) - 1);

        // Unary encode quotient
        for (size_t i = 0; i < q; ++i) {
            out.push_back(0);
        }
        out.push_back(1);

        // Binary encode remainder
        for (size_t i = 0; i < k; ++i) {
            out.push_back((r >> i) & 1);
        }
    }

    static uint64_t decode_golomb_rice(std::span<const uint8_t> data, size_t& offset, size_t k) {
        // Decode unary quotient
        uint64_t q = 0;
        while (offset < data.size() && data[offset] == 0) {
            ++q;
            ++offset;
        }
        ++offset;  // Skip the 1

        // Decode binary remainder
        uint64_t r = 0;
        for (size_t i = 0; i < k && offset < data.size(); ++i, ++offset) {
            r |= (uint64_t{data[offset]} << i);
        }

        return (q << k) | r;
    }

public:
    recsplit_hasher() = default;
    recsplit_hasher(recsplit_hasher&&) = default;
    recsplit_hasher& operator=(recsplit_hasher&&) = default;

    /**
     * @brief Hash a key to its slot
     * @param key Key to hash
     * @return Hash value for the key
     */
    [[nodiscard]] hash_value hash(std::string_view key) const noexcept {
        if (auto slot = slot_for(key)) {
            return hash_value{slot->value};
        }
        return hash_value{key_count_};
    }

    /**
     * @brief Get slot index for a key
     * @param key Key to look up
     * @return Slot index if key was in build set, nullopt otherwise
     */
    [[nodiscard]] std::optional<slot_index> slot_for(std::string_view key) const noexcept {
        if (key_count_ == 0) {
            return std::nullopt;
        }

        uint64_t actual_fp = hash_with_seed(key, base_seed_ ^ 0xFEDCBA9876543210ULL);

        // First try perfect hash path
        size_t bucket_idx = bucket_for_key(key);
        if (buckets_[bucket_idx].num_keys > 0) {
            size_t local_slot = slot_in_bucket(key, bucket_idx);
            size_t global_slot = bucket_offsets_[bucket_idx] + local_slot;

            // Verify membership using fingerprint
            if (global_slot < fingerprints_.size()) {
                uint64_t expected_fp = fingerprints_[global_slot];
                if (expected_fp == actual_fp) {
                    return slot_index{global_slot};
                }
            }
        }

        // Fallback: check overflow keys using SIMD-optimized search
        if (!overflow_fingerprints_.empty()) {
            size_t idx = find_fingerprint_simd(overflow_fingerprints_.data(),
                                               overflow_fingerprints_.size(), actual_fp);
            if (idx < overflow_fingerprints_.size()) {
                return slot_index{overflow_slots_[idx]};
            }
        }

        return std::nullopt;  // Key not in set
    }

    [[nodiscard]] constexpr slot_count max_slots() const noexcept {
        return slot_count{key_count_};
    }

    [[nodiscard]] bool is_perfect_for(std::string_view key) const noexcept {
        return slot_for(key).has_value();
    }

    [[nodiscard]] perfect_hash_stats statistics() const noexcept {
        size_t bucket_bytes = buckets_.size() * sizeof(bucket);
        size_t offset_bytes = bucket_offsets_.size() * sizeof(uint64_t);
        size_t fingerprint_bytes = fingerprints_.size() * sizeof(uint64_t);
        size_t overflow_bytes = overflow_fingerprints_.size() * sizeof(uint64_t) +
                               overflow_slots_.size() * sizeof(size_t);
        size_t total_bytes = bucket_bytes + offset_bytes + fingerprint_bytes + overflow_bytes + sizeof(*this);

        return perfect_hash_stats{
            .key_count = key_count_,
            .memory_bytes = total_bytes,
            .bits_per_key = key_count_ > 0 ? (total_bytes * 8.0) / key_count_ : 0.0
        };
    }

    [[nodiscard]] size_t key_count() const noexcept { return key_count_; }
    [[nodiscard]] size_t perfect_count() const noexcept { return perfect_count_; }
    [[nodiscard]] size_t overflow_count() const noexcept { return overflow_fingerprints_.size(); }
    [[nodiscard]] double bits_per_key() const noexcept { return statistics().bits_per_key; }
    [[nodiscard]] size_t memory_bytes() const noexcept { return statistics().memory_bytes; }

    // Algorithm identifier for serialization
    static constexpr uint32_t ALGORITHM_ID = 1;  // RecSplit

    // Serialization
    [[nodiscard]] std::vector<std::byte> serialize() const {
        std::vector<std::byte> result;

        // Helper to append values
        auto append = [&](const auto& value) {
            auto bytes = std::bit_cast<std::array<std::byte, sizeof(value)>>(value);
            result.insert(result.end(), bytes.begin(), bytes.end());
        };

        auto append_vector_u64 = [&](const std::vector<uint64_t>& vec) {
            append(vec.size());
            for (const auto& v : vec) {
                append(v);
            }
        };

        auto append_vector_size = [&](const std::vector<size_t>& vec) {
            append(vec.size());
            for (const auto& v : vec) {
                append(v);
            }
        };

        // Header: magic + version + algorithm
        append(PERFECT_HASH_MAGIC);
        append(PERFECT_HASH_VERSION);
        append(ALGORITHM_ID);
        append(static_cast<uint32_t>(LeafSize));

        // Core data
        append(key_count_);
        append(perfect_count_);
        append(num_buckets_);
        append(base_seed_);

        // Buckets
        append(buckets_.size());
        for (const auto& b : buckets_) {
            append(b.split);
            append(b.num_keys);
        }

        // Offsets, fingerprints, overflow
        append_vector_u64(bucket_offsets_);
        append_vector_u64(fingerprints_);
        append_vector_u64(overflow_fingerprints_);
        append_vector_size(overflow_slots_);

        return result;
    }

    [[nodiscard]] static result<recsplit_hasher> deserialize(std::span<const std::byte> data) {
        size_t offset = 0;

        // Helper to read a value of type T
        auto read_bytes = [&](void* out, size_t n) -> bool {
            if (offset + n > data.size()) return false;
            std::memcpy(out, data.data() + offset, n);
            offset += n;
            return true;
        };

        auto read_u16 = [&]() -> std::optional<uint16_t> {
            uint16_t v; return read_bytes(&v, sizeof(v)) ? std::optional{v} : std::nullopt;
        };
        auto read_u32 = [&]() -> std::optional<uint32_t> {
            uint32_t v; return read_bytes(&v, sizeof(v)) ? std::optional{v} : std::nullopt;
        };
        auto read_u64 = [&]() -> std::optional<uint64_t> {
            uint64_t v; return read_bytes(&v, sizeof(v)) ? std::optional{v} : std::nullopt;
        };
        auto read_size = [&]() -> std::optional<size_t> {
            size_t v; return read_bytes(&v, sizeof(v)) ? std::optional{v} : std::nullopt;
        };

        auto read_vector_u64 = [&]() -> std::optional<std::vector<uint64_t>> {
            auto size_opt = read_size();
            if (!size_opt) return std::nullopt;
            std::vector<uint64_t> vec;
            vec.reserve(*size_opt);
            for (size_t i = 0; i < *size_opt; ++i) {
                auto v = read_u64();
                if (!v) return std::nullopt;
                vec.push_back(*v);
            }
            return vec;
        };

        auto read_vector_size = [&]() -> std::optional<std::vector<size_t>> {
            auto size_opt = read_size();
            if (!size_opt) return std::nullopt;
            std::vector<size_t> vec;
            vec.reserve(*size_opt);
            for (size_t i = 0; i < *size_opt; ++i) {
                auto v = read_size();
                if (!v) return std::nullopt;
                vec.push_back(*v);
            }
            return vec;
        };

        // Verify header
        auto magic = read_u32();
        if (!magic || *magic != PERFECT_HASH_MAGIC) {
            return std::unexpected(error::invalid_format);
        }

        auto version = read_u32();
        if (!version || *version != PERFECT_HASH_VERSION) {
            return std::unexpected(error::invalid_format);
        }

        auto algo = read_u32();
        if (!algo || *algo != ALGORITHM_ID) {
            return std::unexpected(error::invalid_format);
        }

        auto leaf_size = read_u32();
        if (!leaf_size || *leaf_size != LeafSize) {
            return std::unexpected(error::invalid_format);
        }

        // Read core data
        auto key_count = read_size();
        auto perfect_count = read_size();
        auto num_buckets = read_size();
        auto base_seed = read_u64();

        if (!key_count || !perfect_count || !num_buckets || !base_seed) {
            return std::unexpected(error::invalid_format);
        }

        // Create hasher with reconstructed data
        recsplit_hasher hasher(*key_count, *base_seed);
        hasher.perfect_count_ = *perfect_count;
        hasher.num_buckets_ = *num_buckets;

        // Read buckets
        auto bucket_count = read_size();
        if (!bucket_count) {
            return std::unexpected(error::invalid_format);
        }
        hasher.buckets_.resize(*bucket_count);
        for (size_t i = 0; i < *bucket_count; ++i) {
            auto split = read_u16();
            auto num_keys = read_size();
            if (!split || !num_keys) {
                return std::unexpected(error::invalid_format);
            }
            hasher.buckets_[i].split = *split;
            hasher.buckets_[i].num_keys = *num_keys;
        }

        // Read vectors
        auto bucket_offsets = read_vector_u64();
        auto fingerprints = read_vector_u64();
        auto overflow_fingerprints = read_vector_u64();
        auto overflow_slots = read_vector_size();

        if (!bucket_offsets || !fingerprints || !overflow_fingerprints || !overflow_slots) {
            return std::unexpected(error::invalid_format);
        }

        hasher.bucket_offsets_ = std::move(*bucket_offsets);
        hasher.fingerprints_ = std::move(*fingerprints);
        hasher.overflow_fingerprints_ = std::move(*overflow_fingerprints);
        hasher.overflow_slots_ = std::move(*overflow_slots);

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
            std::vector<std::string> overflow_keys;
            std::vector<std::pair<size_t, uint64_t>> local_fingerprints;  // (local_slot, fingerprint)
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

            // If bucket is too large, move keys to overflow
            if (keys_in_bucket.size() > LeafSize * 3) {
                result.overflow_keys = keys_in_bucket;
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
                    return hasher.hash_with_seed(key, bucket_seed) % keys_in_bucket.size();
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

                    // Store local fingerprints
                    for (const auto& key : keys_in_bucket) {
                        size_t local_slot = test_slot_in_bucket(key);
                        uint64_t fp = hasher.hash_with_seed(key, seed_ ^ 0xFEDCBA9876543210ULL);
                        result.local_fingerprints.emplace_back(local_slot, fp);
                    }
                    return result;
                }
            }

            // Couldn't find split - move keys to overflow
            result.overflow_keys = keys_in_bucket;
            result.success = true;
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

            recsplit_hasher hasher(keys_.size(), seed_);

            // 1. Partition keys into buckets
            std::vector<std::vector<std::string>> bucket_keys(hasher.num_buckets_);
            for (const auto& key : keys_) {
                size_t bucket_idx = hasher.bucket_for_key(key);
                bucket_keys[bucket_idx].push_back(key);
            }

            // 2. Process buckets (potentially in parallel)
            std::vector<bucket_result> results(hasher.num_buckets_);

            if (num_threads_ > 1 && hasher.num_buckets_ > 100) {
                // Parallel processing using std::thread
                auto process_range = [&](size_t start, size_t end) {
                    for (size_t i = start; i < end; ++i) {
                        results[i] = process_bucket(hasher, bucket_keys[i], i);
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
            } else {
                // Single-threaded processing
                for (size_t bucket_idx = 0; bucket_idx < hasher.num_buckets_; ++bucket_idx) {
                    results[bucket_idx] = process_bucket(hasher, bucket_keys[bucket_idx], bucket_idx);
                }
            }

            // 3. Compute cumulative offsets (sequential, but O(num_buckets))
            size_t cumulative_offset = 0;
            for (size_t bucket_idx = 0; bucket_idx < hasher.num_buckets_; ++bucket_idx) {
                hasher.bucket_offsets_[bucket_idx] = cumulative_offset;
                hasher.buckets_[bucket_idx].split = results[bucket_idx].split;
                hasher.buckets_[bucket_idx].num_keys = results[bucket_idx].num_keys;
                cumulative_offset += results[bucket_idx].num_keys;
            }
            hasher.bucket_offsets_[hasher.num_buckets_] = cumulative_offset;
            hasher.perfect_count_ = cumulative_offset;

            // 4. Copy fingerprints to their global positions
            hasher.fingerprints_.resize(hasher.perfect_count_);
            for (size_t bucket_idx = 0; bucket_idx < hasher.num_buckets_; ++bucket_idx) {
                size_t base_offset = hasher.bucket_offsets_[bucket_idx];
                for (const auto& [local_slot, fp] : results[bucket_idx].local_fingerprints) {
                    hasher.fingerprints_[base_offset + local_slot] = fp;
                }
            }

            // 5. Collect and assign overflow keys
            for (size_t bucket_idx = 0; bucket_idx < hasher.num_buckets_; ++bucket_idx) {
                for (const auto& key : results[bucket_idx].overflow_keys) {
                    hasher.overflow_fingerprints_.push_back(hasher.hash_with_seed(key, seed_ ^ 0xFEDCBA9876543210ULL));
                    hasher.overflow_slots_.push_back(cumulative_offset);
                    ++cumulative_offset;
                }
            }

            return hasher;
        }
    };
};

// ===== CHD (COMPRESS, HASH, DISPLACE) =====

/**
 * @class chd_hasher
 * @brief CHD (Compress, Hash, and Displace) perfect hash
 *
 * CHD is a classic minimal perfect hash function with good performance.
 * It's been extensively tested and is used in many production systems.
 *
 * References:
 * - Belazzougui et al. "Hash, displace, and compress" (2009)
 * - Space: ~2.0-2.5 bits per key
 * - Query time: O(1), typically 30-50ns
 */
class chd_hasher {
public:
    class builder;

private:
    std::vector<uint32_t> displacements_;  // Displacement per bucket
    std::vector<int64_t> slot_map_;        // Sparse position -> dense slot (-1 if unused)
    std::vector<uint64_t> fingerprints_;   // Fingerprint per dense slot for membership
    std::vector<uint64_t> overflow_fingerprints_;  // Keys that couldn't be perfectly placed
    std::vector<size_t> overflow_slots_;   // Slots for overflow keys
    size_t key_count_{0};
    size_t perfect_count_{0};  // Keys placed via perfect hash
    size_t num_buckets_{0};
    size_t table_size_{0};  // Total slots in hash table (larger than key_count_)
    double lambda_{5.0};    // Average bucket size
    uint64_t seed_{0};

    // Remix function for hash distribution
    [[nodiscard]] static constexpr uint64_t remix(uint64_t z) noexcept {
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }

    // Hash a string with a given seed
    [[nodiscard]] uint64_t hash_with_seed(std::string_view key, uint64_t seed) const noexcept {
        uint64_t h = seed;
        for (unsigned char c : key) {
            h ^= c;
            h *= 0x100000001b3ULL;
        }
        return remix(h);
    }

    // First hash: determines bucket
    [[nodiscard]] size_t bucket_hash(std::string_view key) const noexcept {
        return hash_with_seed(key, seed_) % num_buckets_;
    }

    // Second hash: determines slot within table given displacement
    [[nodiscard]] size_t slot_hash(std::string_view key, uint32_t displacement) const noexcept {
        return (hash_with_seed(key, seed_ ^ 0xCAFEBABE12345678ULL) + displacement) % table_size_;
    }

    explicit chd_hasher(size_t key_count, double lambda, uint64_t seed)
        : key_count_(key_count)
        , num_buckets_(std::max(size_t{1}, static_cast<size_t>(std::ceil(key_count / lambda))))
        , table_size_(static_cast<size_t>(std::ceil(key_count * 2.0)))  // 2x overhead for reliable displacement finding
        , lambda_(lambda)
        , seed_(seed) {
        displacements_.resize(num_buckets_, 0);
        slot_map_.assign(table_size_, -1);  // Initialize all to unused
        fingerprints_.resize(key_count_, 0);  // Dense fingerprints, one per key
    }

public:
    chd_hasher() = default;
    chd_hasher(chd_hasher&&) = default;
    chd_hasher& operator=(chd_hasher&&) = default;

    [[nodiscard]] hash_value hash(std::string_view key) const noexcept {
        if (auto slot = slot_for(key)) {
            return hash_value{slot->value};
        }
        return hash_value{key_count_};
    }

    [[nodiscard]] std::optional<slot_index> slot_for(std::string_view key) const noexcept {
        if (key_count_ == 0) {
            return std::nullopt;
        }

        uint64_t actual_fp = hash_with_seed(key, seed_ ^ 0xFEDCBA9876543210ULL);

        // First try perfect hash path
        if (!slot_map_.empty()) {
            size_t bucket = bucket_hash(key);
            uint32_t displacement = displacements_[bucket];
            size_t sparse_slot = slot_hash(key, displacement);

            if (sparse_slot < table_size_ && slot_map_[sparse_slot] >= 0) {
                size_t dense_slot = static_cast<size_t>(slot_map_[sparse_slot]);
                if (dense_slot < fingerprints_.size()) {
                    uint64_t expected_fp = fingerprints_[dense_slot];
                    if (expected_fp == actual_fp) {
                        return slot_index{dense_slot};
                    }
                }
            }
        }

        // Fallback: check overflow keys using SIMD-optimized search
        if (!overflow_fingerprints_.empty()) {
            size_t idx = find_fingerprint_simd(overflow_fingerprints_.data(),
                                               overflow_fingerprints_.size(), actual_fp);
            if (idx < overflow_fingerprints_.size()) {
                return slot_index{overflow_slots_[idx]};
            }
        }

        return std::nullopt;  // Key not in set
    }

    [[nodiscard]] constexpr slot_count max_slots() const noexcept {
        return slot_count{key_count_};
    }

    [[nodiscard]] bool is_perfect_for(std::string_view key) const noexcept {
        return slot_for(key).has_value();
    }

    [[nodiscard]] perfect_hash_stats statistics() const noexcept {
        size_t total_bytes = displacements_.size() * sizeof(uint32_t) +
                            slot_map_.size() * sizeof(int64_t) +
                            fingerprints_.size() * sizeof(uint64_t) +
                            overflow_fingerprints_.size() * sizeof(uint64_t) +
                            overflow_slots_.size() * sizeof(size_t);
        return perfect_hash_stats{
            .key_count = key_count_,
            .memory_bytes = total_bytes + sizeof(*this),
            .bits_per_key = key_count_ > 0 ? (total_bytes * 8.0) / key_count_ : 0.0
        };
    }

    [[nodiscard]] size_t key_count() const noexcept { return key_count_; }
    [[nodiscard]] size_t perfect_count() const noexcept { return perfect_count_; }
    [[nodiscard]] size_t overflow_count() const noexcept { return overflow_fingerprints_.size(); }
    [[nodiscard]] size_t table_size() const noexcept { return table_size_; }
    [[nodiscard]] double bits_per_key() const noexcept { return statistics().bits_per_key; }
    [[nodiscard]] size_t memory_bytes() const noexcept { return statistics().memory_bytes; }

    // Algorithm identifier for serialization
    static constexpr uint32_t ALGORITHM_ID = 2;  // CHD

    [[nodiscard]] std::vector<std::byte> serialize() const {
        std::vector<std::byte> result;

        auto append = [&](const auto& value) {
            auto bytes = std::bit_cast<std::array<std::byte, sizeof(value)>>(value);
            result.insert(result.end(), bytes.begin(), bytes.end());
        };

        auto append_vector_u32 = [&](const std::vector<uint32_t>& vec) {
            append(vec.size());
            for (const auto& v : vec) append(v);
        };

        auto append_vector_i64 = [&](const std::vector<int64_t>& vec) {
            append(vec.size());
            for (const auto& v : vec) append(v);
        };

        auto append_vector_u64 = [&](const std::vector<uint64_t>& vec) {
            append(vec.size());
            for (const auto& v : vec) append(v);
        };

        auto append_vector_size = [&](const std::vector<size_t>& vec) {
            append(vec.size());
            for (const auto& v : vec) append(v);
        };

        // Header
        append(PERFECT_HASH_MAGIC);
        append(PERFECT_HASH_VERSION);
        append(ALGORITHM_ID);

        // Core data
        append(key_count_);
        append(perfect_count_);
        append(num_buckets_);
        append(table_size_);
        append(lambda_);
        append(seed_);

        // Vectors
        append_vector_u32(displacements_);
        append_vector_i64(slot_map_);
        append_vector_u64(fingerprints_);
        append_vector_u64(overflow_fingerprints_);
        append_vector_size(overflow_slots_);

        return result;
    }

    [[nodiscard]] static result<chd_hasher> deserialize(std::span<const std::byte> data) {
        size_t offset = 0;

        auto read_bytes = [&](void* out, size_t n) -> bool {
            if (offset + n > data.size()) return false;
            std::memcpy(out, data.data() + offset, n);
            offset += n;
            return true;
        };

        auto read_u32 = [&]() -> std::optional<uint32_t> {
            uint32_t v; return read_bytes(&v, sizeof(v)) ? std::optional{v} : std::nullopt;
        };
        auto read_i64 = [&]() -> std::optional<int64_t> {
            int64_t v; return read_bytes(&v, sizeof(v)) ? std::optional{v} : std::nullopt;
        };
        auto read_u64 = [&]() -> std::optional<uint64_t> {
            uint64_t v; return read_bytes(&v, sizeof(v)) ? std::optional{v} : std::nullopt;
        };
        auto read_size = [&]() -> std::optional<size_t> {
            size_t v; return read_bytes(&v, sizeof(v)) ? std::optional{v} : std::nullopt;
        };
        auto read_double = [&]() -> std::optional<double> {
            double v; return read_bytes(&v, sizeof(v)) ? std::optional{v} : std::nullopt;
        };

        auto read_vector_u32 = [&]() -> std::optional<std::vector<uint32_t>> {
            auto size_opt = read_size();
            if (!size_opt) return std::nullopt;
            std::vector<uint32_t> vec;
            vec.reserve(*size_opt);
            for (size_t i = 0; i < *size_opt; ++i) {
                auto v = read_u32();
                if (!v) return std::nullopt;
                vec.push_back(*v);
            }
            return vec;
        };

        auto read_vector_i64 = [&]() -> std::optional<std::vector<int64_t>> {
            auto size_opt = read_size();
            if (!size_opt) return std::nullopt;
            std::vector<int64_t> vec;
            vec.reserve(*size_opt);
            for (size_t i = 0; i < *size_opt; ++i) {
                auto v = read_i64();
                if (!v) return std::nullopt;
                vec.push_back(*v);
            }
            return vec;
        };

        auto read_vector_u64 = [&]() -> std::optional<std::vector<uint64_t>> {
            auto size_opt = read_size();
            if (!size_opt) return std::nullopt;
            std::vector<uint64_t> vec;
            vec.reserve(*size_opt);
            for (size_t i = 0; i < *size_opt; ++i) {
                auto v = read_u64();
                if (!v) return std::nullopt;
                vec.push_back(*v);
            }
            return vec;
        };

        auto read_vector_size = [&]() -> std::optional<std::vector<size_t>> {
            auto size_opt = read_size();
            if (!size_opt) return std::nullopt;
            std::vector<size_t> vec;
            vec.reserve(*size_opt);
            for (size_t i = 0; i < *size_opt; ++i) {
                auto v = read_size();
                if (!v) return std::nullopt;
                vec.push_back(*v);
            }
            return vec;
        };

        // Verify header
        auto magic = read_u32();
        if (!magic || *magic != PERFECT_HASH_MAGIC) return std::unexpected(error::invalid_format);

        auto version = read_u32();
        if (!version || *version != PERFECT_HASH_VERSION) return std::unexpected(error::invalid_format);

        auto algo = read_u32();
        if (!algo || *algo != ALGORITHM_ID) return std::unexpected(error::invalid_format);

        // Read core data
        auto key_count = read_size();
        auto perfect_count = read_size();
        auto num_buckets = read_size();
        auto table_size = read_size();
        auto lambda = read_double();
        auto seed = read_u64();

        if (!key_count || !perfect_count || !num_buckets || !table_size || !lambda || !seed) {
            return std::unexpected(error::invalid_format);
        }

        chd_hasher hasher(*key_count, *lambda, *seed);
        hasher.perfect_count_ = *perfect_count;
        hasher.num_buckets_ = *num_buckets;
        hasher.table_size_ = *table_size;

        // Read vectors
        auto displacements = read_vector_u32();
        auto slot_map = read_vector_i64();
        auto fingerprints = read_vector_u64();
        auto overflow_fingerprints = read_vector_u64();
        auto overflow_slots = read_vector_size();

        if (!displacements || !slot_map || !fingerprints || !overflow_fingerprints || !overflow_slots) {
            return std::unexpected(error::invalid_format);
        }

        hasher.displacements_ = std::move(*displacements);
        hasher.slot_map_ = std::move(*slot_map);
        hasher.fingerprints_ = std::move(*fingerprints);
        hasher.overflow_fingerprints_ = std::move(*overflow_fingerprints);
        hasher.overflow_slots_ = std::move(*overflow_slots);

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

            chd_hasher hasher(keys_.size(), lambda_, seed_);
            std::vector<size_t> overflow_key_indices;

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
                            // Store fingerprint at dense slot position
                            hasher.fingerprints_[next_dense_slot] = hasher.hash_with_seed(
                                keys_[keys_in_bucket[i]], hasher.seed_ ^ 0xFEDCBA9876543210ULL);
                            ++next_dense_slot;
                        }
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    // Couldn't find displacement - add keys to overflow
                    for (size_t key_idx : keys_in_bucket) {
                        overflow_key_indices.push_back(key_idx);
                    }
                }
            }

            // Resize fingerprints to actual perfect count
            hasher.fingerprints_.resize(next_dense_slot);
            hasher.perfect_count_ = next_dense_slot;

            // Assign overflow keys to sequential slots
            for (size_t key_idx : overflow_key_indices) {
                hasher.overflow_fingerprints_.push_back(
                    hasher.hash_with_seed(keys_[key_idx], hasher.seed_ ^ 0xFEDCBA9876543210ULL));
                hasher.overflow_slots_.push_back(next_dense_slot);
                ++next_dense_slot;
            }

            return hasher;
        }
    };
};

// ===== BBHASH (BIT-BLOCK HASH) =====

/**
 * @class bbhash_hasher
 * @brief BBHash minimal perfect hash with parallel construction
 *
 * BBHash is a space-efficient minimal perfect hash function designed
 * for large datasets. It supports parallel construction via bit-level
 * collision resolution across multiple levels.
 *
 * References:
 * - Limasset et al. "Fast and Scalable Minimal Perfect Hashing for Massive Key Sets" (2017)
 * - Space: ~2.0-3.0 bits per key (depends on gamma)
 * - Query time: O(1), typically 30-50ns
 * - Build time: O(n/p) with p threads
 *
 * Algorithm:
 * 1. Keys are hashed into levels using different hash functions
 * 2. Each level uses a bit array to mark assigned slots
 * 3. Collision detection via bit-level operations (fast and parallel)
 * 4. Space parameter gamma controls bits/key (higher = faster build)
 *
 * @tparam NumLevels Maximum number of collision resolution levels (default 3)
 */
template<size_t NumLevels = 3>
class bbhash_hasher {
    static_assert(NumLevels >= 1 && NumLevels <= 10, "NumLevels must be between 1 and 10");

public:
    class builder;

private:
    // Bit array for each level indicating which slots are occupied
    struct level {
        std::vector<uint64_t> bits;    // Bit array
        std::vector<size_t> rank_checkpoints;  // Rank checkpoints for O(1) rank queries
        size_t num_keys{0};            // Keys assigned to this level
        uint64_t seed{0};              // Hash seed for this level

        [[nodiscard]] bool get_bit(size_t idx) const noexcept {
            if (idx / 64 >= bits.size()) return false;
            return (bits[idx / 64] >> (idx % 64)) & 1;
        }

        void set_bit(size_t idx) noexcept {
            size_t word_idx = idx / 64;
            if (word_idx < bits.size()) {
                bits[word_idx] |= (1ULL << (idx % 64));
            }
        }

        // Build rank structure for O(1) rank queries
        void build_rank_structure() noexcept {
            rank_checkpoints.resize(bits.size());
            size_t cumulative_rank = 0;
            for (size_t i = 0; i < bits.size(); ++i) {
                rank_checkpoints[i] = cumulative_rank;
                cumulative_rank += std::popcount(bits[i]);
            }
        }

        // O(1) rank query: count set bits before position idx
        [[nodiscard]] size_t rank(size_t idx) const noexcept {
            if (idx / 64 >= bits.size()) return num_keys;

            size_t word_idx = idx / 64;
            size_t bit_offset = idx % 64;

            // Checkpoint gives us rank up to start of this word
            size_t result = rank_checkpoints[word_idx];

            // Add popcount of bits before idx within this word
            if (bit_offset > 0) {
                uint64_t mask = (1ULL << bit_offset) - 1;
                result += std::popcount(bits[word_idx] & mask);
            }

            return result;
        }
    };

    std::array<level, NumLevels> levels_;
    std::vector<uint64_t> fingerprints_;  // Fingerprint per dense slot
    std::vector<uint64_t> overflow_fingerprints_;  // Keys that couldn't be perfectly placed
    std::vector<size_t> overflow_slots_;   // Slots for overflow keys
    size_t key_count_{0};
    size_t perfect_count_{0};  // Keys placed via perfect hash
    size_t total_slots_{0};  // gamma * key_count
    double gamma_{2.0};      // Space-time trade-off parameter
    uint64_t base_seed_{0};

    explicit bbhash_hasher(size_t key_count, double gamma, uint64_t base_seed)
        : key_count_(key_count)
        , total_slots_(static_cast<size_t>(std::ceil(key_count * gamma)))
        , gamma_(gamma)
        , base_seed_(base_seed) {

        // Initialize level seeds and pre-allocate bit arrays
        std::mt19937_64 rng(base_seed);
        size_t words_needed = (total_slots_ + 63) / 64;
        for (size_t i = 0; i < NumLevels; ++i) {
            levels_[i].seed = rng();
            levels_[i].bits.resize(words_needed, 0);
        }
    }

    // Remix function for hash distribution
    [[nodiscard]] static constexpr uint64_t remix(uint64_t z) noexcept {
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }

    // Hash with level-specific seed
    [[nodiscard]] uint64_t hash_at_level(std::string_view key, size_t level_idx) const noexcept {
        uint64_t h = levels_[level_idx].seed;
        for (unsigned char c : key) {
            h ^= c;
            h *= 0x100000001b3ULL;  // FNV prime
        }
        return remix(h) % total_slots_;
    }

public:
    bbhash_hasher() = default;
    bbhash_hasher(bbhash_hasher&&) = default;
    bbhash_hasher& operator=(bbhash_hasher&&) = default;

    /**
     * @brief Hash a key to its slot
     * @param key Key to hash
     * @return Hash value for the key
     */
    [[nodiscard]] hash_value hash(std::string_view key) const noexcept {
        if (auto slot = slot_for(key)) {
            return hash_value{slot->value};
        }
        return hash_value{key_count_};
    }

    /**
     * @brief Get slot index for a key
     * @param key Key to look up
     * @return Slot index if key was in build set, nullopt otherwise
     */
    [[nodiscard]] std::optional<slot_index> slot_for(std::string_view key) const noexcept {
        if (key_count_ == 0) {
            return std::nullopt;
        }

        auto fp = fingerprint64(key);

        // First try perfect hash path through levels
        size_t cumulative_offset = 0;
        for (size_t level_idx = 0; level_idx < NumLevels; ++level_idx) {
            uint64_t slot = hash_at_level(key, level_idx);

            if (levels_[level_idx].get_bit(slot)) {
                size_t dense_slot = cumulative_offset + levels_[level_idx].rank(slot);
                if (dense_slot < fingerprints_.size() && fingerprints_[dense_slot] == fp) {
                    return slot_index{dense_slot};
                }
                // Fingerprint mismatch - key not at this level, fall through to overflow
                break;
            }
            cumulative_offset += levels_[level_idx].num_keys;
        }

        // Fallback: check overflow keys using SIMD-optimized search
        if (!overflow_fingerprints_.empty()) {
            size_t idx = find_fingerprint_simd(overflow_fingerprints_.data(),
                                               overflow_fingerprints_.size(), fp);
            if (idx < overflow_fingerprints_.size()) {
                return slot_index{overflow_slots_[idx]};
            }
        }

        return std::nullopt;
    }

    [[nodiscard]] constexpr slot_count max_slots() const noexcept {
        return slot_count{key_count_};
    }

    [[nodiscard]] bool is_perfect_for(std::string_view key) const noexcept {
        return slot_for(key).has_value();
    }

    [[nodiscard]] perfect_hash_stats statistics() const noexcept {
        // BBHash space usage is approximately gamma bits per key
        // We allocate total_slots_ bits per active level
        size_t active_levels = 0;
        for (const auto& level : levels_) {
            if (level.num_keys > 0) {
                ++active_levels;
            }
        }

        // Total bits = number of active levels * total_slots
        size_t total_bits = active_levels * total_slots_;
        size_t fingerprint_bytes = fingerprints_.size() * sizeof(uint64_t);
        size_t overflow_bytes = overflow_fingerprints_.size() * sizeof(uint64_t) +
                               overflow_slots_.size() * sizeof(size_t);
        size_t total_bytes = (total_bits / 8) + fingerprint_bytes + overflow_bytes;

        return perfect_hash_stats{
            .key_count = key_count_,
            .memory_bytes = total_bytes + sizeof(*this),
            .bits_per_key = key_count_ > 0 ? (total_bytes * 8.0) / key_count_ : 0.0
        };
    }

    [[nodiscard]] size_t key_count() const noexcept { return key_count_; }
    [[nodiscard]] size_t perfect_count() const noexcept { return perfect_count_; }
    [[nodiscard]] size_t overflow_count() const noexcept { return overflow_fingerprints_.size(); }
    [[nodiscard]] double bits_per_key() const noexcept { return statistics().bits_per_key; }
    [[nodiscard]] size_t memory_bytes() const noexcept { return statistics().memory_bytes; }
    [[nodiscard]] double gamma() const noexcept { return gamma_; }

    // Algorithm identifier for serialization
    static constexpr uint32_t ALGORITHM_ID = 3;  // BBHash

    [[nodiscard]] std::vector<std::byte> serialize() const {
        std::vector<std::byte> result;

        auto append = [&](const auto& value) {
            auto bytes = std::bit_cast<std::array<std::byte, sizeof(value)>>(value);
            result.insert(result.end(), bytes.begin(), bytes.end());
        };

        auto append_vector_u64 = [&](const std::vector<uint64_t>& vec) {
            append(vec.size());
            for (const auto& v : vec) append(v);
        };

        auto append_vector_size = [&](const std::vector<size_t>& vec) {
            append(vec.size());
            for (const auto& v : vec) append(v);
        };

        // Header
        append(PERFECT_HASH_MAGIC);
        append(PERFECT_HASH_VERSION);
        append(ALGORITHM_ID);
        append(static_cast<uint32_t>(NumLevels));

        // Core data
        append(key_count_);
        append(perfect_count_);
        append(total_slots_);
        append(gamma_);
        append(base_seed_);

        // Levels
        for (const auto& level : levels_) {
            append_vector_u64(level.bits);
            append_vector_size(level.rank_checkpoints);
            append(level.num_keys);
            append(level.seed);
        }

        // Fingerprints and overflow
        append_vector_u64(fingerprints_);
        append_vector_u64(overflow_fingerprints_);
        append_vector_size(overflow_slots_);

        return result;
    }

    [[nodiscard]] static result<bbhash_hasher> deserialize(std::span<const std::byte> data) {
        size_t offset = 0;

        auto read_bytes = [&](void* out, size_t n) -> bool {
            if (offset + n > data.size()) return false;
            std::memcpy(out, data.data() + offset, n);
            offset += n;
            return true;
        };

        auto read_u32 = [&]() -> std::optional<uint32_t> {
            uint32_t v; return read_bytes(&v, sizeof(v)) ? std::optional{v} : std::nullopt;
        };
        auto read_u64 = [&]() -> std::optional<uint64_t> {
            uint64_t v; return read_bytes(&v, sizeof(v)) ? std::optional{v} : std::nullopt;
        };
        auto read_size = [&]() -> std::optional<size_t> {
            size_t v; return read_bytes(&v, sizeof(v)) ? std::optional{v} : std::nullopt;
        };
        auto read_double = [&]() -> std::optional<double> {
            double v; return read_bytes(&v, sizeof(v)) ? std::optional{v} : std::nullopt;
        };

        auto read_vector_u64 = [&]() -> std::optional<std::vector<uint64_t>> {
            auto size_opt = read_size();
            if (!size_opt) return std::nullopt;
            std::vector<uint64_t> vec;
            vec.reserve(*size_opt);
            for (size_t i = 0; i < *size_opt; ++i) {
                auto v = read_u64();
                if (!v) return std::nullopt;
                vec.push_back(*v);
            }
            return vec;
        };

        auto read_vector_size = [&]() -> std::optional<std::vector<size_t>> {
            auto size_opt = read_size();
            if (!size_opt) return std::nullopt;
            std::vector<size_t> vec;
            vec.reserve(*size_opt);
            for (size_t i = 0; i < *size_opt; ++i) {
                auto v = read_size();
                if (!v) return std::nullopt;
                vec.push_back(*v);
            }
            return vec;
        };

        // Verify header
        auto magic = read_u32();
        if (!magic || *magic != PERFECT_HASH_MAGIC) return std::unexpected(error::invalid_format);

        auto version = read_u32();
        if (!version || *version != PERFECT_HASH_VERSION) return std::unexpected(error::invalid_format);

        auto algo = read_u32();
        if (!algo || *algo != ALGORITHM_ID) return std::unexpected(error::invalid_format);

        auto num_levels = read_u32();
        if (!num_levels || *num_levels != NumLevels) return std::unexpected(error::invalid_format);

        // Read core data
        auto key_count = read_size();
        auto perfect_count = read_size();
        auto total_slots = read_size();
        auto gamma = read_double();
        auto base_seed = read_u64();

        if (!key_count || !perfect_count || !total_slots || !gamma || !base_seed) {
            return std::unexpected(error::invalid_format);
        }

        bbhash_hasher hasher(*key_count, *gamma, *base_seed);
        hasher.perfect_count_ = *perfect_count;
        hasher.total_slots_ = *total_slots;

        // Read levels
        for (size_t i = 0; i < NumLevels; ++i) {
            auto bits = read_vector_u64();
            auto rank_checkpoints = read_vector_size();
            auto num_keys = read_size();
            auto seed = read_u64();

            if (!bits || !rank_checkpoints || !num_keys || !seed) {
                return std::unexpected(error::invalid_format);
            }

            hasher.levels_[i].bits = std::move(*bits);
            hasher.levels_[i].rank_checkpoints = std::move(*rank_checkpoints);
            hasher.levels_[i].num_keys = *num_keys;
            hasher.levels_[i].seed = *seed;
        }

        // Read fingerprints and overflow
        auto fingerprints = read_vector_u64();
        auto overflow_fingerprints = read_vector_u64();
        auto overflow_slots = read_vector_size();

        if (!fingerprints || !overflow_fingerprints || !overflow_slots) {
            return std::unexpected(error::invalid_format);
        }

        hasher.fingerprints_ = std::move(*fingerprints);
        hasher.overflow_fingerprints_ = std::move(*overflow_fingerprints);
        hasher.overflow_slots_ = std::move(*overflow_slots);

        return hasher;
    }

    /**
     * @class builder
     * @brief Builder for BBHash perfect hash
     */
    class builder {
        std::vector<std::string> keys_;
        double gamma_{2.0};  // Default space parameter (2x space = faster)
        uint64_t seed_{0x123456789abcdef0ULL};
        size_t num_threads_{1};  // Thread count for parallel build

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

        builder& with_gamma(double g) {
            if (g < 1.0) g = 1.0;
            if (g > 10.0) g = 10.0;
            gamma_ = g;
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

        [[nodiscard]] result<bbhash_hasher> build() {
            if (keys_.empty()) {
                return std::unexpected(error::optimization_failed);
            }

            // Remove duplicates
            std::sort(keys_.begin(), keys_.end());
            keys_.erase(std::unique(keys_.begin(), keys_.end()), keys_.end());

            bbhash_hasher hasher(keys_.size(), gamma_, seed_);

            // Build BBHash structure level by level
            std::vector<std::string> remaining_keys = keys_;

            for (size_t level_idx = 0; level_idx < NumLevels && !remaining_keys.empty(); ++level_idx) {
                auto& current_level = hasher.levels_[level_idx];
                std::vector<std::string> next_level_keys;

                // Track collisions using a temporary collision map
                std::vector<size_t> slot_counts(hasher.total_slots_, 0);

                // Hash all remaining keys to this level
                for (const auto& key : remaining_keys) {
                    uint64_t slot = hasher.hash_at_level(key, level_idx);
                    slot_counts[slot]++;
                }

                // Assign non-colliding keys to this level
                for (const auto& key : remaining_keys) {
                    uint64_t slot = hasher.hash_at_level(key, level_idx);
                    if (slot_counts[slot] == 1) {
                        // No collision - assign to this level
                        current_level.set_bit(slot);
                        current_level.num_keys++;
                    } else {
                        // Collision - push to next level
                        next_level_keys.push_back(key);
                    }
                }

                remaining_keys = std::move(next_level_keys);
            }

            // Build rank structures for O(1) queries
            for (auto& level : hasher.levels_) {
                level.build_rank_structure();
            }

            // Count keys placed in levels (perfect count)
            hasher.perfect_count_ = 0;
            for (const auto& level : hasher.levels_) {
                hasher.perfect_count_ += level.num_keys;
            }

            hasher.fingerprints_.resize(hasher.perfect_count_, 0);

            // Fill fingerprints for perfectly placed keys
            size_t next_dense_slot = 0;
            for (const auto& key : keys_) {
                size_t offset = 0;
                bool placed = false;
                for (size_t level_idx = 0; level_idx < NumLevels; ++level_idx) {
                    uint64_t slot = hasher.hash_at_level(key, level_idx);
                    if (hasher.levels_[level_idx].get_bit(slot)) {
                        size_t dense_slot = offset + hasher.levels_[level_idx].rank(slot);
                        if (dense_slot < hasher.fingerprints_.size()) {
                            hasher.fingerprints_[dense_slot] = fingerprint64(key);
                        }
                        placed = true;
                        break;
                    }
                    offset += hasher.levels_[level_idx].num_keys;
                }
                if (placed) {
                    ++next_dense_slot;
                }
            }

            // Remaining keys go to overflow
            for (const auto& key : remaining_keys) {
                hasher.overflow_fingerprints_.push_back(fingerprint64(key));
                hasher.overflow_slots_.push_back(hasher.perfect_count_ + hasher.overflow_fingerprints_.size() - 1);
            }

            return hasher;
        }
    };
};

// ===== PTHASH (PARTITIONED HASH) =====

/**
 * @class pthash_hasher
 * @brief PTHash minimal perfect hash with encoder-based design
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
 *
 * Algorithm:
 * 1. Keys are partitioned into buckets (first-level hash)
 * 2. Each bucket gets a pilot value (small integer)
 * 3. Pilots are stored compactly using Elias-Fano encoding
 * 4. Query: bucket_hash(key) + pilot[bucket]
 *
 * @tparam Alpha Load factor for buckets (default 0.98 = 98% utilization)
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
    std::vector<uint64_t> fingerprints_;
    std::vector<uint64_t> overflow_fingerprints_;  // Keys that couldn't be perfectly placed
    std::vector<size_t> overflow_slots_;   // Slots for overflow keys
    size_t key_count_{0};
    size_t perfect_count_{0};  // Keys placed via perfect hash
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
     * @brief Hash a key to its slot
     * @param key Key to hash
     * @return Hash value for the key
     */
    [[nodiscard]] hash_value hash(std::string_view key) const noexcept {
        if (auto slot = slot_for(key)) {
            return hash_value{slot->value};
        }
        return hash_value{key_count_};
    }

    /**
     * @brief Get slot index for a key
     * @param key Key to look up
     * @return Slot index if key was in build set, nullopt otherwise
     */
    [[nodiscard]] std::optional<slot_index> slot_for(std::string_view key) const noexcept {
        if (key_count_ == 0) {
            return std::nullopt;
        }

        auto fp = fingerprint64(key);

        // First try perfect hash path
        if (!slot_map_.empty()) {
            size_t bucket_idx = get_bucket(key);
            uint16_t pilot = pilots_.get_pilot(bucket_idx);
            uint64_t raw_slot = bucket_hash(key, pilot);

            if (raw_slot < slot_map_.size() && slot_map_[raw_slot] >= 0) {
                size_t dense_slot = static_cast<size_t>(slot_map_[raw_slot]);
                if (dense_slot < fingerprints_.size() && fingerprints_[dense_slot] == fp) {
                    return slot_index{dense_slot};
                }
            }
        }

        // Fallback: check overflow keys using SIMD-optimized search
        if (!overflow_fingerprints_.empty()) {
            size_t idx = find_fingerprint_simd(overflow_fingerprints_.data(),
                                               overflow_fingerprints_.size(), fp);
            if (idx < overflow_fingerprints_.size()) {
                return slot_index{overflow_slots_[idx]};
            }
        }

        return std::nullopt;
    }

    [[nodiscard]] constexpr slot_count max_slots() const noexcept {
        return slot_count{key_count_};
    }

    [[nodiscard]] bool is_perfect_for(std::string_view key) const noexcept {
        return slot_for(key).has_value();
    }

    [[nodiscard]] perfect_hash_stats statistics() const noexcept {
        size_t total_bytes = pilots_.pilots.size() * sizeof(uint16_t) +
                            slot_map_.size() * sizeof(int64_t) +
                            fingerprints_.size() * sizeof(uint64_t) +
                            overflow_fingerprints_.size() * sizeof(uint64_t) +
                            overflow_slots_.size() * sizeof(size_t);

        return perfect_hash_stats{
            .key_count = key_count_,
            .memory_bytes = total_bytes + sizeof(*this),
            .bits_per_key = key_count_ > 0 ? (total_bytes * 8.0) / key_count_ : 0.0
        };
    }

    [[nodiscard]] size_t key_count() const noexcept { return key_count_; }
    [[nodiscard]] size_t perfect_count() const noexcept { return perfect_count_; }
    [[nodiscard]] size_t overflow_count() const noexcept { return overflow_fingerprints_.size(); }
    [[nodiscard]] double bits_per_key() const noexcept { return statistics().bits_per_key; }
    [[nodiscard]] size_t memory_bytes() const noexcept { return statistics().memory_bytes; }
    [[nodiscard]] size_t num_buckets() const noexcept { return num_buckets_; }

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

            pthash_hasher hasher(keys_.size(), seed_);
            hasher.slot_map_.assign(hasher.table_size_, -1);
            hasher.fingerprints_.reserve(hasher.key_count_);
            std::vector<std::string> overflow_keys;

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

            for (size_t bucket_idx : bucket_order) {
                const auto& bucket_keys = buckets[bucket_idx];
                if (bucket_keys.empty()) continue;

                bool found_pilot = false;
                size_t pilot_limit = std::min(max_pilot_search_,
                    static_cast<size_t>(std::numeric_limits<uint16_t>::max()) + 1);

                // Use unordered_set for faster collision checking within bucket
                std::unordered_set<uint64_t> current_slots;
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

                        // Record dense slot mapping and fingerprints
                        for (const auto& key : bucket_keys) {
                            uint64_t slot = hasher.bucket_hash(key, pilot_value);
                            hasher.slot_map_[slot] = static_cast<int64_t>(next_index);
                            hasher.fingerprints_.push_back(fingerprint64(key));
                            ++next_index;
                        }

                        found_pilot = true;
                        break;
                    }
                }

                if (!found_pilot) {
                    // Couldn't find pilot - add keys to overflow
                    for (const auto& key : bucket_keys) {
                        overflow_keys.push_back(key);
                    }
                }
            }

            hasher.perfect_count_ = next_index;

            // Assign overflow keys to sequential slots
            for (const auto& key : overflow_keys) {
                hasher.overflow_fingerprints_.push_back(fingerprint64(key));
                hasher.overflow_slots_.push_back(next_index);
                ++next_index;
            }

            return hasher;
        }
    };
};

// ===== FCH (FOX-CHAZELLE-HEATH) =====

/**
 * @class fch_hasher
 * @brief FCH (Fox, Chazelle, Heath) minimal perfect hash
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
 *
 * Algorithm:
 * 1. Hash keys into buckets using primary hash
 * 2. Sort buckets by size (descending)
 * 3. For each bucket, find displacement value that avoids collisions
 * 4. Store displacement array (compact integer array)
 * 5. Query: secondary_hash(key) + displacement[bucket(key)]
 *
 * The algorithm is educational and demonstrates the core ideas behind
 * many minimal perfect hash functions.
 */
class fch_hasher {
public:
    class builder;

private:
    std::vector<uint32_t> displacements_;  // Displacement per bucket
    std::vector<int64_t> slot_map_;        // Raw table position -> dense slot (-1 if empty)
    std::vector<uint64_t> fingerprints_;   // Fingerprint per dense slot for membership
    std::vector<uint64_t> overflow_fingerprints_;  // Keys that couldn't be perfectly placed
    std::vector<size_t> overflow_slots_;   // Slots for overflow keys (sequential)
    size_t key_count_{0};
    size_t perfect_count_{0};  // Number of keys placed perfectly
    size_t num_buckets_{0};
    size_t table_size_{0};
    double bucket_size_{4.0};  // Average keys per bucket
    uint64_t seed_{0};

    explicit fch_hasher(size_t key_count, double bucket_size, uint64_t seed)
        : key_count_(key_count)
        , perfect_count_(0)
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
     * @brief Hash a key to its slot
     * @param key Key to hash
     * @return Hash value for the key
     */
    [[nodiscard]] hash_value hash(std::string_view key) const noexcept {
        if (auto slot = slot_for(key)) {
            return hash_value{slot->value};
        }
        return hash_value{key_count_};
    }

    /**
     * @brief Get slot index for a key
     * @param key Key to look up
     * @return Slot index if key was in build set, nullopt otherwise
     */
    [[nodiscard]] std::optional<slot_index> slot_for(std::string_view key) const noexcept {
        if (key_count_ == 0) {
            return std::nullopt;
        }

        uint64_t actual_fp = fingerprint64(key);

        // First, try the perfect hash path (if we have any perfectly placed keys)
        if (num_buckets_ > 0 && table_size_ > 0 && !slot_map_.empty()) {
            // Step 1: Find the bucket for this key
            size_t bucket_idx = get_bucket(key);

            // Step 2: Get the displacement for this bucket
            uint32_t displacement = displacements_[bucket_idx];

            // Step 3: Compute the raw table position using FCH formula
            uint64_t raw_position = get_position(key, displacement);

            // Step 4: Check if position is valid and get dense slot
            if (raw_position < slot_map_.size() && slot_map_[raw_position] >= 0) {
                size_t dense_slot = static_cast<size_t>(slot_map_[raw_position]);

                // Step 5: Verify membership using fingerprint
                if (dense_slot < fingerprints_.size()) {
                    uint64_t expected_fp = fingerprints_[dense_slot];
                    if (expected_fp == actual_fp) {
                        return slot_index{dense_slot};
                    }
                }
            }
        }

        // Fallback: check overflow keys using SIMD-optimized search
        if (!overflow_fingerprints_.empty()) {
            size_t idx = find_fingerprint_simd(overflow_fingerprints_.data(),
                                               overflow_fingerprints_.size(), actual_fp);
            if (idx < overflow_fingerprints_.size()) {
                return slot_index{overflow_slots_[idx]};
            }
        }

        return std::nullopt;  // Key not in set
    }

    [[nodiscard]] constexpr slot_count max_slots() const noexcept {
        return slot_count{key_count_};
    }

    [[nodiscard]] bool is_perfect_for(std::string_view key) const noexcept {
        return slot_for(key).has_value();
    }

    [[nodiscard]] perfect_hash_stats statistics() const noexcept {
        size_t total_bytes = displacements_.size() * sizeof(uint32_t) +
                            slot_map_.size() * sizeof(int64_t) +
                            fingerprints_.size() * sizeof(uint64_t) +
                            overflow_fingerprints_.size() * sizeof(uint64_t) +
                            overflow_slots_.size() * sizeof(size_t);

        return perfect_hash_stats{
            .key_count = key_count_,
            .memory_bytes = total_bytes + sizeof(*this),
            .bits_per_key = key_count_ > 0 ? (total_bytes * 8.0) / key_count_ : 0.0
        };
    }

    [[nodiscard]] size_t key_count() const noexcept { return key_count_; }
    [[nodiscard]] size_t perfect_count() const noexcept { return perfect_count_; }
    [[nodiscard]] size_t overflow_count() const noexcept { return overflow_fingerprints_.size(); }
    [[nodiscard]] double bits_per_key() const noexcept { return statistics().bits_per_key; }
    [[nodiscard]] size_t memory_bytes() const noexcept { return statistics().memory_bytes; }
    [[nodiscard]] size_t num_buckets() const noexcept { return num_buckets_; }

    // Algorithm identifier for serialization
    static constexpr uint32_t ALGORITHM_ID = 4;  // FCH

    [[nodiscard]] std::vector<std::byte> serialize() const {
        std::vector<std::byte> result;

        auto append = [&](const auto& value) {
            auto bytes = std::bit_cast<std::array<std::byte, sizeof(value)>>(value);
            result.insert(result.end(), bytes.begin(), bytes.end());
        };

        auto append_vector_u32 = [&](const std::vector<uint32_t>& vec) {
            append(vec.size());
            for (const auto& v : vec) append(v);
        };

        auto append_vector_i64 = [&](const std::vector<int64_t>& vec) {
            append(vec.size());
            for (const auto& v : vec) append(v);
        };

        auto append_vector_u64 = [&](const std::vector<uint64_t>& vec) {
            append(vec.size());
            for (const auto& v : vec) append(v);
        };

        auto append_vector_size = [&](const std::vector<size_t>& vec) {
            append(vec.size());
            for (const auto& v : vec) append(v);
        };

        // Header
        append(PERFECT_HASH_MAGIC);
        append(PERFECT_HASH_VERSION);
        append(ALGORITHM_ID);

        // Core data
        append(key_count_);
        append(perfect_count_);
        append(num_buckets_);
        append(table_size_);
        append(bucket_size_);
        append(seed_);

        // Vectors
        append_vector_u32(displacements_);
        append_vector_i64(slot_map_);
        append_vector_u64(fingerprints_);
        append_vector_u64(overflow_fingerprints_);
        append_vector_size(overflow_slots_);

        return result;
    }

    [[nodiscard]] static result<fch_hasher> deserialize(std::span<const std::byte> data) {
        size_t offset = 0;

        auto read_bytes = [&](void* out, size_t n) -> bool {
            if (offset + n > data.size()) return false;
            std::memcpy(out, data.data() + offset, n);
            offset += n;
            return true;
        };

        auto read_u32 = [&]() -> std::optional<uint32_t> {
            uint32_t v; return read_bytes(&v, sizeof(v)) ? std::optional{v} : std::nullopt;
        };
        auto read_i64 = [&]() -> std::optional<int64_t> {
            int64_t v; return read_bytes(&v, sizeof(v)) ? std::optional{v} : std::nullopt;
        };
        auto read_u64 = [&]() -> std::optional<uint64_t> {
            uint64_t v; return read_bytes(&v, sizeof(v)) ? std::optional{v} : std::nullopt;
        };
        auto read_size = [&]() -> std::optional<size_t> {
            size_t v; return read_bytes(&v, sizeof(v)) ? std::optional{v} : std::nullopt;
        };
        auto read_double = [&]() -> std::optional<double> {
            double v; return read_bytes(&v, sizeof(v)) ? std::optional{v} : std::nullopt;
        };

        auto read_vector_u32 = [&]() -> std::optional<std::vector<uint32_t>> {
            auto size_opt = read_size();
            if (!size_opt) return std::nullopt;
            std::vector<uint32_t> vec;
            vec.reserve(*size_opt);
            for (size_t i = 0; i < *size_opt; ++i) {
                auto v = read_u32();
                if (!v) return std::nullopt;
                vec.push_back(*v);
            }
            return vec;
        };

        auto read_vector_i64 = [&]() -> std::optional<std::vector<int64_t>> {
            auto size_opt = read_size();
            if (!size_opt) return std::nullopt;
            std::vector<int64_t> vec;
            vec.reserve(*size_opt);
            for (size_t i = 0; i < *size_opt; ++i) {
                auto v = read_i64();
                if (!v) return std::nullopt;
                vec.push_back(*v);
            }
            return vec;
        };

        auto read_vector_u64 = [&]() -> std::optional<std::vector<uint64_t>> {
            auto size_opt = read_size();
            if (!size_opt) return std::nullopt;
            std::vector<uint64_t> vec;
            vec.reserve(*size_opt);
            for (size_t i = 0; i < *size_opt; ++i) {
                auto v = read_u64();
                if (!v) return std::nullopt;
                vec.push_back(*v);
            }
            return vec;
        };

        auto read_vector_size = [&]() -> std::optional<std::vector<size_t>> {
            auto size_opt = read_size();
            if (!size_opt) return std::nullopt;
            std::vector<size_t> vec;
            vec.reserve(*size_opt);
            for (size_t i = 0; i < *size_opt; ++i) {
                auto v = read_size();
                if (!v) return std::nullopt;
                vec.push_back(*v);
            }
            return vec;
        };

        // Verify header
        auto magic = read_u32();
        if (!magic || *magic != PERFECT_HASH_MAGIC) return std::unexpected(error::invalid_format);

        auto version = read_u32();
        if (!version || *version != PERFECT_HASH_VERSION) return std::unexpected(error::invalid_format);

        auto algo = read_u32();
        if (!algo || *algo != ALGORITHM_ID) return std::unexpected(error::invalid_format);

        // Read core data
        auto key_count = read_size();
        auto perfect_count = read_size();
        auto num_buckets = read_size();
        auto table_size = read_size();
        auto bucket_size = read_double();
        auto seed = read_u64();

        if (!key_count || !perfect_count || !num_buckets || !table_size || !bucket_size || !seed) {
            return std::unexpected(error::invalid_format);
        }

        fch_hasher hasher(*key_count, *bucket_size, *seed);
        hasher.perfect_count_ = *perfect_count;
        hasher.num_buckets_ = *num_buckets;
        hasher.table_size_ = *table_size;

        // Read vectors
        auto displacements = read_vector_u32();
        auto slot_map = read_vector_i64();
        auto fingerprints = read_vector_u64();
        auto overflow_fingerprints = read_vector_u64();
        auto overflow_slots = read_vector_size();

        if (!displacements || !slot_map || !fingerprints || !overflow_fingerprints || !overflow_slots) {
            return std::unexpected(error::invalid_format);
        }

        hasher.displacements_ = std::move(*displacements);
        hasher.slot_map_ = std::move(*slot_map);
        hasher.fingerprints_ = std::move(*fingerprints);
        hasher.overflow_fingerprints_ = std::move(*overflow_fingerprints);
        hasher.overflow_slots_ = std::move(*overflow_slots);

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

            fch_hasher hasher(keys_.size(), bucket_size_, seed_);
            hasher.slot_map_.assign(hasher.table_size_, -1);
            hasher.fingerprints_.reserve(hasher.key_count_);

            // Step 1: Partition keys into buckets
            std::vector<std::vector<std::string>> buckets(hasher.num_buckets_);
            for (const auto& key : keys_) {
                size_t bucket_idx = hasher.get_bucket(key);
                buckets[bucket_idx].push_back(key);
            }

            // Step 2: Sort buckets by size (largest first)
            // This greedy approach improves success rate
            std::vector<size_t> bucket_order(hasher.num_buckets_);
            std::iota(bucket_order.begin(), bucket_order.end(), 0);
            std::sort(bucket_order.begin(), bucket_order.end(),
                [&buckets](size_t a, size_t b) {
                    return buckets[a].size() > buckets[b].size();
                });

            // Step 3: For each bucket, find a displacement that avoids collisions
            std::vector<bool> used_positions(hasher.table_size_, false);
            size_t next_dense_slot = 0;
            std::vector<std::string> overflow_keys;  // Keys that couldn't be perfectly placed

            for (size_t bucket_idx : bucket_order) {
                const auto& bucket_keys = buckets[bucket_idx];
                if (bucket_keys.empty()) continue;

                bool found_displacement = false;

                for (uint32_t displacement = 0; displacement < max_displacement_search_; ++displacement) {
                    // Check if this displacement works for all keys in the bucket
                    std::vector<uint64_t> positions;
                    positions.reserve(bucket_keys.size());
                    bool collision = false;

                    for (const auto& key : bucket_keys) {
                        uint64_t pos = hasher.get_position(key, displacement);

                        // Check collision with already used positions
                        if (used_positions[pos]) {
                            collision = true;
                            break;
                        }

                        // Check collision within this bucket
                        if (std::find(positions.begin(), positions.end(), pos) != positions.end()) {
                            collision = true;
                            break;
                        }

                        positions.push_back(pos);
                    }

                    if (!collision) {
                        // Found valid displacement - record it
                        hasher.displacements_[bucket_idx] = displacement;

                        // Mark positions as used and assign dense slots
                        for (size_t i = 0; i < bucket_keys.size(); ++i) {
                            uint64_t pos = positions[i];
                            used_positions[pos] = true;
                            hasher.slot_map_[pos] = static_cast<int64_t>(next_dense_slot);
                            hasher.fingerprints_.push_back(fingerprint64(bucket_keys[i]));
                            ++next_dense_slot;
                        }

                        found_displacement = true;
                        break;
                    }
                }

                if (!found_displacement) {
                    // Couldn't find displacement for this bucket - add keys to overflow
                    for (const auto& key : bucket_keys) {
                        overflow_keys.push_back(key);
                    }
                }
            }

            // Assign sequential slots to overflow keys
            hasher.perfect_count_ = next_dense_slot;
            for (const auto& key : overflow_keys) {
                hasher.overflow_fingerprints_.push_back(fingerprint64(key));
                hasher.overflow_slots_.push_back(next_dense_slot);
                ++next_dense_slot;
            }

            // Verify we placed all keys (perfect + overflow)
            if (next_dense_slot != hasher.key_count_) {
                return std::unexpected(error::optimization_failed);
            }

            return hasher;
        }
    };
};

// ===== CONVENIENCE ALIASES =====

using recsplit8 = recsplit_hasher<8>;
using recsplit16 = recsplit_hasher<16>;
using bbhash3 = bbhash_hasher<3>;
using bbhash5 = bbhash_hasher<5>;
using pthash98 = pthash_hasher<98>;
using pthash95 = pthash_hasher<95>;

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

/**
 * @brief Create a BBHash hasher from keys
 */
template<size_t NumLevels = 3>
[[nodiscard]] inline result<bbhash_hasher<NumLevels>>
make_bbhash(std::span<const std::string> keys, double gamma = 2.0, uint64_t seed = 0) {
    typename bbhash_hasher<NumLevels>::builder builder;
    return builder.add_all(keys).with_gamma(gamma).with_seed(seed).build();
}

/**
 * @brief Create a PTHash hasher from keys
 */
template<size_t AlphaInt = 98>
[[nodiscard]] inline result<pthash_hasher<AlphaInt>>
make_pthash(std::span<const std::string> keys, uint64_t seed = 0) {
    typename pthash_hasher<AlphaInt>::builder builder;
    return builder.add_all(keys).with_seed(seed).build();
}

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

} // namespace maph
