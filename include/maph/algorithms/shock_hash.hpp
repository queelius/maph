/**
 * @file shock_hash.hpp
 * @brief Bucketed 2-choice cuckoo MPHF with choice bits via ribbon retrieval.
 *
 * A working implementation of the ShockHash family (Lehmann, Sanders,
 * Walzer 2023-2024). Keys are partitioned into fixed-size buckets; each
 * bucket runs an independent 2-choice cuckoo placement with a seed
 * search. The "choice bit" (which of each key's two hash functions it
 * ended up using) is stored globally via ribbon_retrieval<1>.
 *
 * Space:
 *   - Global seed (64 bits, amortized to zero per key)
 *   - Per-bucket seed: 32 bits / BucketSize = 32/BucketSize bits/key
 *     (e.g. 0.5 bits/key at BucketSize=64)
 *   - Ribbon retrieval of 1-bit choices: ~1.08 bits/key
 *   - Total: ~1.58 bits/key at BucketSize=64 (theoretical floor ~1.44)
 *
 * Query:
 *   - hash key to bucket
 *   - load bucket seed, compute two candidate positions within bucket
 *   - ribbon lookup to pick which position
 *   - return bucket_base + chosen position (non-minimal: bucket_base * BucketSize)
 *
 * Minimality: this implementation is minimal (range_size == num_keys)
 * only when every bucket packs exactly BucketSize keys. In general
 * some buckets underfill, which means range_size slightly exceeds
 * num_keys (the "tail" of the bucket-size Poisson distribution).
 *
 * This is a single-session simplified implementation, not the full
 * paper. The paper further compresses bucket seeds via variable-width
 * coding and achieves ~1.5 bits/key.
 */

#pragma once

#include "../concepts/perfect_hash_function.hpp"
#include "../core.hpp"
#include "../detail/cuckoo_orient.hpp"
#include "../detail/hash.hpp"
#include "../detail/serialization.hpp"
#include "../retrieval/ribbon_retrieval.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace maph {

template <size_t BucketSize = 64>
    requires (BucketSize >= 4 && BucketSize <= 256)
class shock_hash {
public:
    static constexpr size_t bucket_size = BucketSize;

    shock_hash() = default;

    // ===== perfect_hash_function interface =====

    [[nodiscard]] slot_index slot_for(std::string_view key) const noexcept {
        uint32_t b = bucket_for(key);
        uint32_t bseed = bucket_seeds_[b];
        auto [p1, p2] = bucket_positions(key, bseed);
        uint8_t choice = static_cast<uint8_t>(choices_.lookup(key)) & 1u;
        uint32_t chosen = (choice == 0) ? p1 : p2;
        return slot_index{static_cast<uint64_t>(b) * bucket_size + chosen};
    }

    [[nodiscard]] size_t num_keys() const noexcept { return num_keys_; }
    [[nodiscard]] size_t range_size() const noexcept {
        return static_cast<size_t>(num_buckets_) * bucket_size;
    }

    [[nodiscard]] double bits_per_key() const noexcept {
        if (num_keys_ == 0) return 0.0;
        double seed_bits = static_cast<double>(num_buckets_) *
                           static_cast<double>(sizeof(uint32_t) * 8);
        double ribbon_bits = choices_.bits_per_key() *
                             static_cast<double>(num_keys_);
        return (seed_bits + ribbon_bits) / static_cast<double>(num_keys_);
    }

    [[nodiscard]] size_t memory_bytes() const noexcept {
        return sizeof(global_seed_) +
               bucket_seeds_.size() * sizeof(uint32_t) +
               choices_.memory_bytes();
    }

    [[nodiscard]] size_t num_buckets() const noexcept { return num_buckets_; }

    [[nodiscard]] std::vector<std::byte> serialize() const {
        std::vector<std::byte> out;
        auto append = [&](const auto& v) {
            auto b = std::bit_cast<std::array<std::byte, sizeof(v)>>(v);
            out.insert(out.end(), b.begin(), b.end());
        };
        append(static_cast<uint32_t>(bucket_size));
        append(global_seed_);
        append(static_cast<uint64_t>(num_keys_));
        append(static_cast<uint64_t>(num_buckets_));
        for (auto s : bucket_seeds_) append(s);
        auto ribbon_bytes = choices_.serialize();
        append(static_cast<uint64_t>(ribbon_bytes.size()));
        out.insert(out.end(), ribbon_bytes.begin(), ribbon_bytes.end());
        return out;
    }

    [[nodiscard]] static result<shock_hash>
    deserialize(std::span<const std::byte> bytes) {
        size_t off = 0;
        auto read = [&](auto& v) -> bool {
            if (off + sizeof(v) > bytes.size()) return false;
            std::memcpy(&v, bytes.data() + off, sizeof(v));
            off += sizeof(v);
            return true;
        };
        uint32_t bsz{};
        uint64_t gseed{}, nkeys{}, nbuckets{};
        if (!read(bsz) || bsz != bucket_size) return std::unexpected(error::invalid_format);
        if (!read(gseed) || !read(nkeys) || !read(nbuckets)) {
            return std::unexpected(error::invalid_format);
        }
        shock_hash out;
        out.global_seed_ = gseed;
        out.num_keys_ = static_cast<size_t>(nkeys);
        out.num_buckets_ = static_cast<size_t>(nbuckets);
        out.bucket_seeds_.resize(out.num_buckets_);
        for (auto& s : out.bucket_seeds_) {
            if (!read(s)) return std::unexpected(error::invalid_format);
        }
        uint64_t ribbon_len{};
        if (!read(ribbon_len)) return std::unexpected(error::invalid_format);
        if (off + ribbon_len > bytes.size()) return std::unexpected(error::invalid_format);
        auto r = ribbon_retrieval<1>::deserialize(
            bytes.subspan(off, static_cast<size_t>(ribbon_len)));
        if (!r) return std::unexpected(r.error());
        out.choices_ = std::move(*r);
        return out;
    }

    // ===== Builder =====

    class builder {
        std::vector<std::string> keys_{};
        uint64_t global_seed_{0x5a5a'5a5a'5a5a'5a5aULL};
        size_t max_seed_trials_{1 << 20};
        size_t max_global_retries_{8};

    public:
        builder() = default;

        builder& add(std::string_view k) { keys_.emplace_back(k); return *this; }
        builder& add_all(std::span<const std::string> ks) {
            keys_.insert(keys_.end(), ks.begin(), ks.end()); return *this;
        }
        builder& add_all(const std::vector<std::string>& ks) {
            keys_.insert(keys_.end(), ks.begin(), ks.end()); return *this;
        }

        builder& with_seed(uint64_t s) { global_seed_ = s; return *this; }
        builder& with_max_seed_trials(size_t t) { max_seed_trials_ = t; return *this; }

        [[nodiscard]] result<shock_hash> build() {
            std::sort(keys_.begin(), keys_.end());
            keys_.erase(std::unique(keys_.begin(), keys_.end()), keys_.end());

            if (keys_.empty()) return std::unexpected(error::optimization_failed);

            std::mt19937_64 rng{global_seed_};
            for (size_t retry = 0; retry < max_global_retries_; ++retry) {
                shock_hash out;
                out.global_seed_ = rng();
                out.num_keys_ = keys_.size();
                // Target bucket load is 0.6 * bucket_size. Under random
                // bucket assignment, Poisson variance pushes occasional
                // buckets above target; the 0.4 slack absorbs those
                // even at 10^5 buckets (which is num_keys ~ 5M). Load
                // factor within a bucket then averages to 0.6, safely
                // below the cuckoo peelability threshold.
                double target_load =
                    static_cast<double>(bucket_size) * 0.6;
                out.num_buckets_ = static_cast<size_t>(
                    std::ceil(static_cast<double>(keys_.size()) / target_load));
                if (out.num_buckets_ == 0) out.num_buckets_ = 1;

                if (attempt_build(out)) {
                    return out;
                }
            }
            return std::unexpected(error::optimization_failed);
        }

    private:
        bool attempt_build(shock_hash& out) {
            // 1. Bucket keys.
            std::vector<std::vector<uint32_t>> bucket_keys(out.num_buckets_);
            for (uint32_t i = 0; i < keys_.size(); ++i) {
                uint32_t b = static_cast<uint32_t>(
                    phf_hash_with_seed(keys_[i], out.global_seed_) % out.num_buckets_);
                bucket_keys[b].push_back(i);
            }

            // Reject if any bucket overflows.
            for (const auto& bk : bucket_keys) {
                if (bk.size() > bucket_size) return false;
            }

            // 2. Per-bucket seed search and cuckoo placement.
            out.bucket_seeds_.assign(out.num_buckets_, 0);
            std::vector<uint8_t> choice_bits(keys_.size(), 0);

            for (uint32_t b = 0; b < out.num_buckets_; ++b) {
                const auto& bk = bucket_keys[b];
                if (bk.empty()) continue;

                bool placed = false;
                for (uint32_t seed_try = 0;
                     seed_try < static_cast<uint32_t>(max_seed_trials_);
                     ++seed_try) {
                    std::vector<std::pair<uint32_t, uint32_t>> edges;
                    edges.reserve(bk.size());
                    for (uint32_t key_idx : bk) {
                        auto [p1, p2] = shock_hash::positions_for_seed(
                            keys_[key_idx], seed_try);
                        edges.emplace_back(p1, p2);
                    }

                    auto orient = detail::cuckoo_orient(edges, bucket_size);
                    if (orient.has_value()) {
                        out.bucket_seeds_[b] = seed_try;
                        for (size_t i = 0; i < bk.size(); ++i) {
                            choice_bits[bk[i]] = orient->assignment[i];
                        }
                        placed = true;
                        break;
                    }
                }
                if (!placed) return false;
            }

            // 3. Build ribbon_retrieval<1> of choice bits keyed by the
            //    original key strings.
            std::vector<uint8_t> vals(keys_.size());
            for (size_t i = 0; i < keys_.size(); ++i) {
                vals[i] = choice_bits[i];
            }
            auto r = typename ribbon_retrieval<1>::builder{}
                .add_all(std::span<const std::string>{keys_},
                         std::span<const uint8_t>{vals})
                .build();
            if (!r) return false;
            out.choices_ = std::move(*r);
            return true;
        }

    };

    // Shared by builder and the query path.
    static std::pair<uint32_t, uint32_t>
    positions_for_seed(std::string_view key, uint32_t seed) noexcept {
        uint64_t h = phf_hash_with_seed(key,
            static_cast<uint64_t>(seed) * 0x9e3779b97f4a7c15ULL);
        uint64_t h_mixed = h;
        h_mixed ^= h_mixed >> 33;
        h_mixed *= 0xff51afd7ed558ccdULL;
        h_mixed ^= h_mixed >> 33;
        uint32_t p1 = static_cast<uint32_t>(h_mixed & 0xFFFFFFFFu) % bucket_size;
        uint32_t p2 = static_cast<uint32_t>(h_mixed >> 32) % bucket_size;
        if (p1 == p2) p2 = (p2 + 1) % bucket_size;
        return {p1, p2};
    }

private:
    // Bucket assignment: the same formula used by the builder.
    uint32_t bucket_for(std::string_view key) const noexcept {
        return static_cast<uint32_t>(
            phf_hash_with_seed(key, global_seed_) % num_buckets_);
    }

    std::pair<uint32_t, uint32_t>
    bucket_positions(std::string_view key, uint32_t bseed) const noexcept {
        return positions_for_seed(key, bseed);
    }

    uint64_t global_seed_{0};
    size_t num_keys_{0};
    size_t num_buckets_{0};
    std::vector<uint32_t> bucket_seeds_{};
    ribbon_retrieval<1> choices_{};
};

static_assert(perfect_hash_function<shock_hash<64>>);

} // namespace maph
