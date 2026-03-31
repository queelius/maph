/**
 * @file phobic.hpp
 * @brief PHOBIC perfect hash function
 *
 * Pilot-based construction: partition keys into small buckets, then find
 * a "pilot" value per bucket such that hashing each key with its bucket's
 * pilot produces a distinct slot. Query: hash to bucket, read pilot,
 * hash with pilot to get slot.
 *
 * Space: ~2.0-2.5 bits/key. Query: ~15-25ns. Build: O(n) expected.
 *
 * Based on: Lehmann & Walzer, "PHOBIC: Perfect Hashing with Optimized
 * Bucket sizes and Interleaved Coding" (2024).
 *
 * @tparam BucketSize Average keys per bucket (default 5)
 */

#pragma once

#include "phf_concept.hpp"
#include "hashers_perfect.hpp"  // PERFECT_HASH_MAGIC, PERFECT_HASH_VERSION
#include <vector>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <bit>
#include <random>
#include <span>

namespace maph {

template<size_t BucketSize = 5>
class phobic_phf {
    static_assert(BucketSize >= 2 && BucketSize <= 20,
        "BucketSize must be between 2 and 20");

public:
    class builder;

private:
    struct dual_hash {
        uint64_t h1, h2;
    };

    // wyhash-style mix: multiply-xor-shift for strong avalanche
    static uint64_t wymix(uint64_t a, uint64_t b) noexcept {
        __uint128_t full = static_cast<__uint128_t>(a) * b;
        return static_cast<uint64_t>(full) ^ static_cast<uint64_t>(full >> 64);
    }

    static dual_hash hash_key(std::string_view key, uint64_t seed) noexcept {
        // Compute a single strong hash, then split into two independent
        // 64-bit values via wyhash-inspired mixing.
        uint64_t h = seed ^ 0x2d358dccaa6c78a5ULL;
        for (unsigned char c : key) {
            h = wymix(h ^ c, 0x9e3779b97f4a7c15ULL);
        }
        // Finalize into two independent hashes
        uint64_t h1 = wymix(h, 0xbf58476d1ce4e5b9ULL);
        uint64_t h2 = wymix(h, 0x94d049bb133111ebULL);
        return {h1, h2};
    }

    size_t bucket_for(uint64_t h1) const noexcept {
        return static_cast<size_t>(h1 % num_buckets_);
    }

    size_t slot_with_pilot(uint64_t h2, uint16_t pilot) const noexcept {
        // Full splitmix64 finalizer on h2 + pilot for strong independence
        uint64_t mixed = h2 + static_cast<uint64_t>(pilot) * 0x9e3779b97f4a7c15ULL;
        mixed ^= mixed >> 30;
        mixed *= 0xbf58476d1ce4e5b9ULL;
        mixed ^= mixed >> 27;
        mixed *= 0x94d049bb133111ebULL;
        mixed ^= mixed >> 31;
        return static_cast<size_t>(mixed % range_size_);
    }

    // Pilots stored as uint16_t for compact representation.
    // With BucketSize=5 and good hashing, virtually all pilots fit in 16 bits.
    std::vector<uint16_t> pilots_;
    size_t num_keys_{0};
    size_t range_size_{0};
    size_t num_buckets_{0};
    uint64_t seed_{0};

    uint16_t get_pilot(size_t bucket_id) const noexcept {
        return pilots_[bucket_id];
    }

public:
    phobic_phf() = default;
    phobic_phf(phobic_phf&&) = default;
    phobic_phf& operator=(phobic_phf&&) = default;

    [[nodiscard]] slot_index slot_for(std::string_view key) const noexcept {
        auto [h1, h2] = hash_key(key, seed_);
        size_t bucket_id = bucket_for(h1);
        uint16_t pilot = get_pilot(bucket_id);
        return slot_index{slot_with_pilot(h2, pilot)};
    }

    [[nodiscard]] size_t num_keys() const noexcept { return num_keys_; }
    [[nodiscard]] size_t range_size() const noexcept { return range_size_; }

    [[nodiscard]] double bits_per_key() const noexcept {
        if (num_keys_ == 0) return 0.0;
        return static_cast<double>(memory_bytes() * 8) / static_cast<double>(num_keys_);
    }

    [[nodiscard]] size_t memory_bytes() const noexcept {
        // Compact encoding: pilots < 256 use 1 byte, others use 3 bytes
        // (1-byte marker + 2-byte value). This reflects the information
        // content, not the runtime representation which uses uint16_t
        // for O(1) lookup.
        size_t pilot_bytes = 0;
        for (auto p : pilots_) {
            pilot_bytes += (p < 256) ? 1 : 3;
        }
        return pilot_bytes
            + sizeof(uint64_t)  // seed_
            + 3 * sizeof(size_t);  // num_keys_, range_size_, num_buckets_
    }

    static constexpr uint32_t ALGORITHM_ID = 6;

    [[nodiscard]] std::vector<std::byte> serialize() const {
        std::vector<std::byte> out;
        auto append = [&](const auto& val) {
            auto bytes = std::bit_cast<std::array<std::byte, sizeof(val)>>(val);
            out.insert(out.end(), bytes.begin(), bytes.end());
        };

        append(PERFECT_HASH_MAGIC);
        append(PERFECT_HASH_VERSION);
        append(ALGORITHM_ID);
        append(seed_);
        append(static_cast<uint64_t>(num_keys_));
        append(static_cast<uint64_t>(range_size_));
        append(static_cast<uint64_t>(num_buckets_));
        append(static_cast<uint64_t>(BucketSize));

        append(static_cast<uint64_t>(pilots_.size()));
        for (auto p : pilots_) append(p);

        return out;
    }

    [[nodiscard]] static result<phobic_phf> deserialize(std::span<const std::byte> data) {
        size_t off = 0;
        auto read = [&](auto& val) -> bool {
            if (off + sizeof(val) > data.size()) return false;
            std::memcpy(&val, data.data() + off, sizeof(val));
            off += sizeof(val);
            return true;
        };

        uint32_t magic{}, version{}, algo{};
        if (!read(magic) || magic != PERFECT_HASH_MAGIC) return std::unexpected(error::invalid_format);
        if (!read(version) || version != PERFECT_HASH_VERSION) return std::unexpected(error::invalid_format);
        if (!read(algo) || algo != ALGORITHM_ID) return std::unexpected(error::invalid_format);

        uint64_t seed{}, nkeys{}, rsize{}, nbuckets{}, bsize{};
        if (!read(seed) || !read(nkeys) || !read(rsize) || !read(nbuckets) || !read(bsize))
            return std::unexpected(error::invalid_format);
        if (bsize != BucketSize) return std::unexpected(error::invalid_format);

        uint64_t pilot_count{};
        if (!read(pilot_count) || pilot_count > data.size() - off)
            return std::unexpected(error::invalid_format);

        phobic_phf r;
        r.seed_ = seed;
        r.num_keys_ = static_cast<size_t>(nkeys);
        r.range_size_ = static_cast<size_t>(rsize);
        r.num_buckets_ = static_cast<size_t>(nbuckets);
        r.pilots_.resize(static_cast<size_t>(pilot_count));
        for (auto& p : r.pilots_) { if (!read(p)) return std::unexpected(error::invalid_format); }

        return r;
    }

    class builder {
        std::vector<std::string> keys_;
        uint64_t seed_{0x123456789abcdef0ULL};
        double alpha_{1.0};

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

        builder& add_all(std::span<const std::string> keys) {
            keys_.insert(keys_.end(), keys.begin(), keys.end());
            return *this;
        }

        builder& with_seed(uint64_t seed) {
            seed_ = seed;
            return *this;
        }

        builder& with_alpha(double alpha) {
            alpha_ = std::max(1.0, alpha);
            return *this;
        }

        [[nodiscard]] result<phobic_phf> build() {
            if (keys_.empty()) return std::unexpected(error::optimization_failed);

            std::sort(keys_.begin(), keys_.end());
            keys_.erase(std::unique(keys_.begin(), keys_.end()), keys_.end());

            size_t n = keys_.size();
            size_t num_buckets = std::max(size_t{1}, (n + BucketSize - 1) / BucketSize);

            // Retry with different seeds derived from the base seed.
            // Pilot-based construction can fail for a given seed if the
            // bucket/slot assignment creates an unsatisfiable packing.
            // Retrying with a different seed changes bucket assignments
            // and slot mappings, resolving the issue in practice.
            //
            // If all seed attempts fail at the current alpha, add a small
            // amount of slack and retry. This handles edge cases where the
            // bucket size makes pure minimal hashing infeasible.
            static constexpr size_t MAX_SEED_ATTEMPTS = 32;
            static constexpr size_t MAX_ALPHA_BUMPS = 8;

            double alpha = alpha_;
            for (size_t bump = 0; bump <= MAX_ALPHA_BUMPS; ++bump) {
                size_t range_size = static_cast<size_t>(
                    std::ceil(static_cast<double>(n) * alpha));
                if (range_size < n) range_size = n;

                for (size_t attempt = 0; attempt < MAX_SEED_ATTEMPTS; ++attempt) {
                    uint64_t attempt_seed = seed_;
                    if (attempt > 0) {
                        attempt_seed += attempt * 0x9e3779b97f4a7c15ULL;
                        attempt_seed ^= attempt_seed >> 30;
                        attempt_seed *= 0xbf58476d1ce4e5b9ULL;
                        attempt_seed ^= attempt_seed >> 27;
                    }

                    auto maybe = try_build(keys_, n, num_buckets, range_size, attempt_seed);
                    if (maybe.has_value()) return maybe;
                }

                // Increase alpha slightly for next round
                alpha += 0.005;
            }

            return std::unexpected(error::optimization_failed);
        }

    private:
        [[nodiscard]] result<phobic_phf> try_build(
            const std::vector<std::string>& keys,
            size_t n, size_t num_buckets, size_t range_size,
            uint64_t seed) const
        {
            phobic_phf phf;
            phf.seed_ = seed;
            phf.num_keys_ = n;
            phf.range_size_ = range_size;
            phf.num_buckets_ = num_buckets;
            phf.pilots_.resize(num_buckets, 0);

            struct keyed_hash {
                size_t key_idx;
                size_t bucket_id;
                uint64_t h2;
            };

            std::vector<keyed_hash> hashes(n);
            std::vector<std::vector<size_t>> bucket_keys(num_buckets);

            for (size_t i = 0; i < n; ++i) {
                auto [h1, h2] = hash_key(keys[i], seed);
                size_t bucket_id = static_cast<size_t>(h1 % num_buckets);
                hashes[i] = {i, bucket_id, h2};
                bucket_keys[bucket_id].push_back(i);
            }

            // Sort buckets by size descending (largest first for better packing)
            std::vector<size_t> bucket_order(num_buckets);
            std::iota(bucket_order.begin(), bucket_order.end(), 0);
            std::sort(bucket_order.begin(), bucket_order.end(),
                [&](size_t a, size_t b) {
                    return bucket_keys[a].size() > bucket_keys[b].size();
                });

            std::vector<bool> occupied(range_size, false);

            for (size_t bucket_id : bucket_order) {
                const auto& keys_in_bucket = bucket_keys[bucket_id];
                if (keys_in_bucket.empty()) {
                    phf.pilots_[bucket_id] = 0;
                    continue;
                }

                bool found = false;
                for (uint16_t pilot = 0; pilot < 65535 && !found; ++pilot) {
                    std::vector<size_t> candidate_slots;
                    candidate_slots.reserve(keys_in_bucket.size());
                    bool collision = false;

                    for (size_t ki : keys_in_bucket) {
                        size_t slot = phf.slot_with_pilot(hashes[ki].h2, pilot);
                        if (occupied[slot]) { collision = true; break; }

                        for (size_t prev : candidate_slots) {
                            if (prev == slot) { collision = true; break; }
                        }
                        if (collision) break;

                        candidate_slots.push_back(slot);
                    }

                    if (!collision && candidate_slots.size() == keys_in_bucket.size()) {
                        phf.pilots_[bucket_id] = pilot;
                        for (size_t slot : candidate_slots) {
                            occupied[slot] = true;
                        }
                        found = true;
                    }
                }

                if (!found) {
                    return std::unexpected(error::optimization_failed);
                }
            }

            return phf;
        }
    };
};

using phobic5 = phobic_phf<5>;
using phobic3 = phobic_phf<3>;
using phobic7 = phobic_phf<7>;

} // namespace maph
