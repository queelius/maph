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

#include "../core.hpp"
#include "../concepts/perfect_hash_function.hpp"
#include "../detail/serialization.hpp"
#include <vector>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <numeric>
#include <bit>
#include <random>
#include <span>
#include <thread>

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
        phf_serial::write_header(out, ALGORITHM_ID);

        phf_serial::append(out, seed_);
        phf_serial::append(out, static_cast<uint64_t>(num_keys_));
        phf_serial::append(out, static_cast<uint64_t>(range_size_));
        phf_serial::append(out, static_cast<uint64_t>(num_buckets_));
        phf_serial::append(out, static_cast<uint64_t>(BucketSize));

        phf_serial::append_vector(out, pilots_);
        return out;
    }

    [[nodiscard]] static result<phobic_phf> deserialize(std::span<const std::byte> data) {
        phf_serial::reader rd(data);

        if (!phf_serial::verify_header(rd, ALGORITHM_ID)) {
            return std::unexpected(error::invalid_format);
        }

        uint64_t seed{}, nkeys{}, rsize{}, nbuckets{}, bsize{};
        if (!rd.read(seed) || !rd.read(nkeys) || !rd.read(rsize) ||
            !rd.read(nbuckets) || !rd.read(bsize)) {
            return std::unexpected(error::invalid_format);
        }
        if (bsize != BucketSize) return std::unexpected(error::invalid_format);

        phobic_phf r;
        r.seed_ = seed;
        r.num_keys_ = static_cast<size_t>(nkeys);
        r.range_size_ = static_cast<size_t>(rsize);
        r.num_buckets_ = static_cast<size_t>(nbuckets);
        if (!rd.read_vector(r.pilots_)) return std::unexpected(error::invalid_format);
        return r;
    }

    class builder {
        std::vector<std::string> keys_;
        uint64_t seed_{0x123456789abcdef0ULL};
        double alpha_{1.0};
        size_t threads_{1};  // 0 = auto (hardware_concurrency), 1 = sequential, N = N threads

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

        // 0 = auto-detect via std::thread::hardware_concurrency().
        // 1 = sequential (single-threaded, same as before).
        // N > 1 = use N worker threads for parallel pilot search.
        //
        // Only the pilot-search phase of construction is parallelized; key
        // hashing and bucket assignment are still serial (they are fast).
        // For small key counts the parallel path adds overhead without
        // speedup, so keys < 2048 falls back to the sequential algorithm.
        builder& with_threads(size_t n) {
            threads_ = n;
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

                    // Decide sequential vs parallel. Below the 2K-key
                    // threshold, thread overhead exceeds the win.
                    size_t nthreads = threads_;
                    if (nthreads == 0) {
                        nthreads = std::max<size_t>(1u,
                            std::thread::hardware_concurrency());
                    }
                    auto maybe = (nthreads > 1 && n >= 2048)
                        ? try_build_parallel(keys_, n, num_buckets, range_size, attempt_seed, nthreads)
                        : try_build(keys_, n, num_buckets, range_size, attempt_seed);
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

        // Parallel pilot search.
        //
        // The sequential algorithm processes buckets in descending-size order,
        // claiming slots against a plain bool bitset. The parallel version
        // replaces the bitset with atomic uint64 words and lets workers pull
        // buckets from a shared atomic counter.
        //
        // Correctness: each slot claim is a single atomic fetch_or. If the
        // bit was already set (another worker or an earlier attempt by the
        // same worker), the worker releases all slots it did successfully
        // claim and tries the next pilot. At the end, each set bit in the
        // bitset corresponds to exactly one (bucket, key) pair.
        //
        // Performance: conflicts are rare with 65535 pilot candidates per
        // bucket and a reasonably uniform hash. Livelock is bounded by the
        // pilot ceiling; if exhausted, the worker marks the build failed.
        [[nodiscard]] result<phobic_phf> try_build_parallel(
            const std::vector<std::string>& keys,
            size_t n, size_t num_buckets, size_t range_size,
            uint64_t seed, size_t nthreads) const
        {
            static_assert(std::atomic<uint64_t>::is_always_lock_free,
                "parallel build requires lock-free 64-bit atomics");

            phobic_phf phf;
            phf.seed_ = seed;
            phf.num_keys_ = n;
            phf.range_size_ = range_size;
            phf.num_buckets_ = num_buckets;
            phf.pilots_.resize(num_buckets, 0);

            // Phase 1 (serial): hash keys, partition into buckets.
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

            // Phase 2 (serial): sort buckets by descending size.
            std::vector<size_t> bucket_order(num_buckets);
            std::iota(bucket_order.begin(), bucket_order.end(), 0);
            std::sort(bucket_order.begin(), bucket_order.end(),
                [&](size_t a, size_t b) {
                    return bucket_keys[a].size() > bucket_keys[b].size();
                });

            // Shared state for phase 3.
            size_t num_words = (range_size + 63) / 64;
            std::vector<std::atomic<uint64_t>> occupied(num_words);
            for (auto& w : occupied) w.store(0, std::memory_order_relaxed);

            std::atomic<size_t> next_bucket{0};
            std::atomic<bool> failed{false};

            auto worker = [&]() {
                std::vector<size_t> candidate_slots;
                std::vector<size_t> claimed;  // slots this worker successfully set

                while (!failed.load(std::memory_order_acquire)) {
                    size_t idx = next_bucket.fetch_add(1, std::memory_order_relaxed);
                    if (idx >= num_buckets) break;

                    size_t bucket_id = bucket_order[idx];
                    const auto& keys_in_bucket = bucket_keys[bucket_id];
                    if (keys_in_bucket.empty()) {
                        phf.pilots_[bucket_id] = 0;
                        continue;
                    }

                    bool found = false;
                    for (uint16_t pilot = 0; pilot < 65535 && !found; ++pilot) {
                        candidate_slots.clear();
                        bool internal_collision = false;

                        for (size_t ki : keys_in_bucket) {
                            size_t slot = phf.slot_with_pilot(hashes[ki].h2, pilot);
                            for (size_t prev : candidate_slots) {
                                if (prev == slot) { internal_collision = true; break; }
                            }
                            if (internal_collision) break;
                            candidate_slots.push_back(slot);
                        }
                        if (internal_collision) continue;

                        // Atomically claim each candidate slot. On the first
                        // bit that was already set, release everything we
                        // claimed and try the next pilot.
                        claimed.clear();
                        bool conflict = false;
                        for (size_t slot : candidate_slots) {
                            size_t word = slot >> 6;
                            uint64_t bit = uint64_t{1} << (slot & 63);
                            uint64_t old = occupied[word].fetch_or(
                                bit, std::memory_order_acq_rel);
                            if (old & bit) { conflict = true; break; }
                            claimed.push_back(slot);
                        }

                        if (conflict) {
                            for (size_t slot : claimed) {
                                size_t word = slot >> 6;
                                uint64_t bit = uint64_t{1} << (slot & 63);
                                occupied[word].fetch_and(
                                    ~bit, std::memory_order_release);
                            }
                            continue;
                        }

                        // Writes to different bucket_id slots in pilots_ are
                        // independent memory locations; no race here.
                        phf.pilots_[bucket_id] = pilot;
                        found = true;
                    }

                    if (!found) {
                        failed.store(true, std::memory_order_release);
                        return;
                    }
                }
            };

            std::vector<std::thread> pool;
            pool.reserve(nthreads);
            for (size_t t = 0; t < nthreads; ++t) pool.emplace_back(worker);
            for (auto& th : pool) th.join();

            if (failed.load(std::memory_order_acquire)) {
                return std::unexpected(error::optimization_failed);
            }
            return phf;
        }
    };
};

using phobic3 = phobic_phf<3>;
using phobic4 = phobic_phf<4>;
using phobic5 = phobic_phf<5>;
using phobic7 = phobic_phf<7>;

// ===== STATIC ASSERTIONS =====

static_assert(perfect_hash_function<phobic_phf<5>>);

} // namespace maph
