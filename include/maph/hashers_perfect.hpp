/**
 * @file hashers_perfect.hpp
 * @brief Perfect hash function implementations satisfying perfect_hash_function concept
 *
 * Provides multiple perfect hash algorithms with a unified interface.
 * Each algorithm maps n keys to distinct slots in [0, n) with O(1) lookup.
 * No fingerprint storage or overflow handling -- builds retry on failure.
 */

#pragma once

#include "core.hpp"
#include "phf_concept.hpp"
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

// Magic numbers for serialization format
constexpr uint32_t PERFECT_HASH_MAGIC = 0x4D415048;  // "MAPH"
constexpr uint32_t PERFECT_HASH_VERSION = 2;

// Serialization requires little-endian. All modern x86/ARM targets are LE.
// If you need big-endian support, add byte-swap wrappers around read/write.
static_assert(std::endian::native == std::endian::little,
    "maph serialization assumes little-endian byte order");

// Maximum element count allowed in a deserialized vector to prevent OOM
// from crafted input. 2^40 elements (~1 trillion) is generous but bounded.
constexpr uint64_t MAX_SERIALIZED_ELEMENT_COUNT = 1ULL << 40;

// ===== SHARED SERIALIZATION HELPERS =====
//
// Small append/read primitives shared by all PHF algorithms. They encode
// counts as uint64_t for 32/64-bit portability and enforce a bound on
// element counts to prevent OOM from crafted input.

namespace phf_serial {

/// Append a trivially-copyable value to the byte buffer.
template<typename T>
inline void append(std::vector<std::byte>& buf, const T& value) {
    auto bytes = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
    buf.insert(buf.end(), bytes.begin(), bytes.end());
}

/// Append a vector with length prefix. Values of type U (or convertible)
/// are written with one append call per element; the count is a uint64_t.
template<typename Vec>
inline void append_vector(std::vector<std::byte>& buf, const Vec& v) {
    append(buf, static_cast<uint64_t>(v.size()));
    for (const auto& e : v) append(buf, e);
}

/// Append a size_t vector, widening each element to uint64_t for portability.
inline void append_vector_size(std::vector<std::byte>& buf,
                               const std::vector<size_t>& v) {
    append(buf, static_cast<uint64_t>(v.size()));
    for (auto e : v) append(buf, static_cast<uint64_t>(e));
}

/// Bounded stream reader over a byte span. Tracks offset, returns false
/// on any short read; never throws.
class reader {
    std::span<const std::byte> data_;
    size_t off_{0};
public:
    explicit reader(std::span<const std::byte> d) noexcept : data_(d) {}

    size_t remaining() const noexcept { return data_.size() - off_; }

    template<typename T>
    [[nodiscard]] bool read(T& out) noexcept {
        if (off_ + sizeof(T) > data_.size()) return false;
        std::memcpy(&out, data_.data() + off_, sizeof(T));
        off_ += sizeof(T);
        return true;
    }

    /// Read a length-prefixed vector of fixed-size T elements.
    template<typename T>
    [[nodiscard]] bool read_vector(std::vector<T>& out) noexcept {
        uint64_t count{};
        if (!read(count) || count > MAX_SERIALIZED_ELEMENT_COUNT) return false;
        auto n = static_cast<size_t>(count);
        if (n > remaining() / sizeof(T)) return false;
        out.resize(n);
        for (auto& e : out) { if (!read(e)) return false; }
        return true;
    }

    /// Read a length-prefixed vector of uint64_t and narrow each to size_t.
    [[nodiscard]] bool read_vector_size(std::vector<size_t>& out) noexcept {
        uint64_t count{};
        if (!read(count) || count > MAX_SERIALIZED_ELEMENT_COUNT) return false;
        auto n = static_cast<size_t>(count);
        if (n > remaining() / sizeof(uint64_t)) return false;
        out.resize(n);
        for (auto& e : out) {
            uint64_t v{};
            if (!read(v)) return false;
            e = static_cast<size_t>(v);
        }
        return true;
    }
};

/// Verify the standard header (magic + version + algorithm id). Optionally
/// reads an additional trailing uint32_t (e.g. LeafSize, NumLevels, AlphaInt)
/// that parameterizes the algorithm. Returns true on match.
inline bool verify_header(reader& r, uint32_t expected_algo,
                          std::optional<uint32_t> expected_param = std::nullopt) {
    uint32_t magic{}, version{}, algo{};
    if (!r.read(magic) || magic != PERFECT_HASH_MAGIC) return false;
    if (!r.read(version) || version != PERFECT_HASH_VERSION) return false;
    if (!r.read(algo) || algo != expected_algo) return false;
    if (expected_param) {
        uint32_t param{};
        if (!r.read(param) || param != *expected_param) return false;
    }
    return true;
}

/// Write the standard header. The param (if provided) is written as uint32_t.
inline void write_header(std::vector<std::byte>& buf, uint32_t algo,
                         std::optional<uint32_t> param = std::nullopt) {
    append(buf, PERFECT_HASH_MAGIC);
    append(buf, PERFECT_HASH_VERSION);
    append(buf, algo);
    if (param) append(buf, *param);
}

}  // namespace phf_serial

// ===== SHARED HASH PRIMITIVES =====
//
// The splitmix64 finalizer and FNV-1a-with-seed are used identically by
// several algorithms. Factored out to avoid duplication.

/// SplitMix64 final mixer — strong avalanche in a few cycles.
[[nodiscard]] inline constexpr uint64_t phf_remix(uint64_t z) noexcept {
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

/// FNV-1a over the key bytes starting from `seed`, finalized with splitmix64.
[[nodiscard]] inline uint64_t phf_hash_with_seed(std::string_view key, uint64_t seed) noexcept {
    uint64_t h = seed;
    for (unsigned char c : key) {
        h ^= c;
        h *= 0x100000001b3ULL;  // FNV prime
    }
    return phf_remix(h);
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
    size_t key_count_{0};
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

    // Hash with level-specific seed
    [[nodiscard]] uint64_t hash_at_level(std::string_view key, size_t level_idx) const noexcept {
        return phf_hash_with_seed(key, levels_[level_idx].seed) % total_slots_;
    }

public:
    bbhash_hasher() = default;
    bbhash_hasher(bbhash_hasher&&) = default;
    bbhash_hasher& operator=(bbhash_hasher&&) = default;

    /**
     * @brief Get slot index for a key (deterministic, no verification)
     * @param key Key to look up
     * @return Slot index in [0, num_keys())
     */
    [[nodiscard]] slot_index slot_for(std::string_view key) const noexcept {
        if (key_count_ == 0) return slot_index{0};

        size_t cumulative_offset = 0;
        for (size_t level_idx = 0; level_idx < NumLevels; ++level_idx) {
            uint64_t slot = hash_at_level(key, level_idx);

            if (levels_[level_idx].get_bit(slot)) {
                return slot_index{cumulative_offset + levels_[level_idx].rank(slot)};
            }
            cumulative_offset += levels_[level_idx].num_keys;
        }

        // Key not in build set, return arbitrary valid index
        return slot_index{0};
    }

    [[nodiscard]] size_t num_keys() const noexcept { return key_count_; }
    [[nodiscard]] size_t range_size() const noexcept { return key_count_; }

    [[nodiscard]] double bits_per_key() const noexcept {
        if (key_count_ == 0) return 0.0;
        // BBHash space usage is approximately gamma bits per key
        size_t active_levels = 0;
        for (const auto& level : levels_) {
            if (level.num_keys > 0) ++active_levels;
        }
        size_t total_bits = active_levels * total_slots_;
        size_t rank_bytes = 0;
        for (const auto& level : levels_) {
            rank_bytes += level.rank_checkpoints.size() * sizeof(size_t);
        }
        size_t total_bytes = (total_bits / 8) + rank_bytes + sizeof(*this);
        return (total_bytes * 8.0) / key_count_;
    }

    [[nodiscard]] size_t memory_bytes() const noexcept {
        size_t active_levels = 0;
        for (const auto& level : levels_) {
            if (level.num_keys > 0) ++active_levels;
        }
        size_t total_bits = active_levels * total_slots_;
        size_t rank_bytes = 0;
        for (const auto& level : levels_) {
            rank_bytes += level.rank_checkpoints.size() * sizeof(size_t);
        }
        return (total_bits / 8) + rank_bytes + sizeof(*this);
    }

    [[nodiscard]] double gamma() const noexcept { return gamma_; }

    // Algorithm identifier for serialization
    static constexpr uint32_t ALGORITHM_ID = 3;  // BBHash

    [[nodiscard]] std::vector<std::byte> serialize() const {
        std::vector<std::byte> out;
        phf_serial::write_header(out, ALGORITHM_ID, static_cast<uint32_t>(NumLevels));

        phf_serial::append(out, static_cast<uint64_t>(key_count_));
        phf_serial::append(out, static_cast<uint64_t>(total_slots_));
        phf_serial::append(out, gamma_);
        phf_serial::append(out, base_seed_);

        for (const auto& lvl : levels_) {
            phf_serial::append_vector(out, lvl.bits);
            phf_serial::append_vector_size(out, lvl.rank_checkpoints);
            phf_serial::append(out, static_cast<uint64_t>(lvl.num_keys));
            phf_serial::append(out, lvl.seed);
        }
        return out;
    }

    [[nodiscard]] static result<bbhash_hasher> deserialize(std::span<const std::byte> data) {
        phf_serial::reader r(data);

        if (!phf_serial::verify_header(r, ALGORITHM_ID, static_cast<uint32_t>(NumLevels))) {
            return std::unexpected(error::invalid_format);
        }

        uint64_t key_count_u64{}, total_slots_u64{}, base_seed{};
        double gamma{};
        if (!r.read(key_count_u64) || !r.read(total_slots_u64) ||
            !r.read(gamma) || !r.read(base_seed)) {
            return std::unexpected(error::invalid_format);
        }
        if (key_count_u64 > MAX_SERIALIZED_ELEMENT_COUNT) {
            return std::unexpected(error::invalid_format);
        }

        bbhash_hasher hasher(static_cast<size_t>(key_count_u64), gamma, base_seed);
        hasher.total_slots_ = static_cast<size_t>(total_slots_u64);

        for (auto& lvl : hasher.levels_) {
            uint64_t num_keys{};
            if (!r.read_vector(lvl.bits) || !r.read_vector_size(lvl.rank_checkpoints) ||
                !r.read(num_keys) || !r.read(lvl.seed)) {
                return std::unexpected(error::invalid_format);
            }
            lvl.num_keys = static_cast<size_t>(num_keys);
        }
        return hasher;
    }

    /**
     * @class builder
     * @brief Builder for BBHash perfect hash
     *
     * Note: BBHash level construction is inherently sequential (each level
     * depends on knowing which keys collided at the previous level).
     * Use RecSplit with_threads() for parallel construction.
     */
    class builder {
        std::vector<std::string> keys_;
        double gamma_{2.0};  // Default space parameter (2x space = faster)
        uint64_t seed_{0x123456789abcdef0ULL};

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

        [[nodiscard]] result<bbhash_hasher> build() {
            if (keys_.empty()) {
                return std::unexpected(error::optimization_failed);
            }

            // Remove duplicates
            std::sort(keys_.begin(), keys_.end());
            keys_.erase(std::unique(keys_.begin(), keys_.end()), keys_.end());

            double current_gamma = gamma_;

            for (int attempt = 0; attempt < 100; ++attempt) {
                uint64_t attempt_seed = seed_ ^ (attempt * 0x9e3779b97f4a7c15ULL);

                // Bump gamma every 5 attempts (more aggressive for large key sets)
                if (attempt > 0 && attempt % 5 == 0) {
                    current_gamma += 0.5;
                }

                bbhash_hasher hasher(keys_.size(), current_gamma, attempt_seed);

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

                // All keys must be placed (no remaining)
                if (!remaining_keys.empty()) {
                    continue;  // Retry with different seed
                }

                // Build rank structures for O(1) queries
                for (auto& level : hasher.levels_) {
                    level.build_rank_structure();
                }

                return hasher;
            }

            return std::unexpected(error::optimization_failed);
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

// ===== STATIC ASSERTIONS =====

static_assert(perfect_hash_function<recsplit_hasher<8>>);
static_assert(perfect_hash_function<chd_hasher>);
static_assert(perfect_hash_function<bbhash_hasher<3>>);
static_assert(perfect_hash_function<fch_hasher>);
static_assert(perfect_hash_function<pthash_hasher<98>>);

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
