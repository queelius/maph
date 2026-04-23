/**
 * @file binary_fuse_filter.hpp
 * @brief Binary fuse filter: 3-wise peeling filter with segmented hashing.
 *
 * Based on Graf and Lemire, "Binary Fuse Filters: Fast and Smaller Than
 * Xor Filters" (JEA 2022). Achieves ~1.13 * M bits/key at false-positive
 * rate 2^-M, roughly 8% less space than xor_filter's ~1.23 * M while
 * keeping the same query cost (3 memory reads + XOR).
 *
 * The space improvement comes from segmented hashing: instead of placing
 * each key's three positions uniformly over the array, place them in
 * three consecutive segments selected by a per-key segment offset. This
 * "sliding window" structure makes the peeling algorithm succeed at a
 * higher load factor, shrinking the overhead.
 *
 * Space:   ~1.125 * M bits per key (empirically).
 * Query:   three memory reads from nearby segments, XOR, compare.
 * FPR:     ~2^-M for M in {8, 16, 32}.
 * Build:   peeling-based; retries with a new seed on failure.
 */

#pragma once

#include "../core.hpp"
#include "../detail/fingerprint_hash.hpp"
#include "../detail/serialization.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace maph {

template <unsigned FingerprintBits>
    requires (FingerprintBits == 8 || FingerprintBits == 16 || FingerprintBits == 32)
class binary_fuse_filter {
public:
    using fp_type = std::conditional_t<FingerprintBits <= 8, uint8_t,
                    std::conditional_t<FingerprintBits <= 16, uint16_t, uint32_t>>;

private:
    static constexpr uint64_t fp_mask = (FingerprintBits == 64)
        ? ~uint64_t{0} : ((uint64_t{1} << FingerprintBits) - 1);

    std::vector<fp_type> table_{};
    size_t segment_length_{0};
    size_t segment_count_{0};
    size_t array_length_{0};
    uint64_t seed_{0};

    struct positions {
        uint32_t h0, h1, h2;
        fp_type fingerprint;
    };

    // Parameters from Graf-Lemire 2022 for arity=3.
    static size_t calc_segment_length(size_t n) {
        if (n == 0) return 4;
        // segment_length ~= n / c, capped. The paper's formula:
        // segment_length = 2 ^ floor(log_3.33(n) + 2.25)
        double x = std::log(static_cast<double>(n)) / std::log(3.33) + 2.25;
        size_t s = size_t{1} << static_cast<int>(x);
        if (s < 4) s = 4;
        if (s > 262144) s = 262144;
        return s;
    }

    static double calc_size_factor(size_t n) {
        // size_factor >= 1.125; grows slightly for small n.
        if (n == 0) return 2.0;
        double f = 0.875 + 0.25 * std::log(1'000'000.0)
                 / std::log(static_cast<double>(n));
        return std::max(1.125, f);
    }

    // Following the Graf-Lemire 2022 reference. A 64-bit key hash is
    // first mapped to a starting offset in [0, segment_count_length),
    // aligned to a segment boundary. Three positions are then that
    // offset + {0, sl, 2*sl}, each XORed with a per-index bit range of
    // the hash for intra-segment entropy.
    positions compute(std::string_view key) const noexcept {
        uint64_t h = membership_fingerprint(key) ^ seed_;

        // Fingerprint from a second mix.
        uint64_t h2 = h;
        h2 ^= h2 >> 33;
        h2 *= 0xff51afd7ed558ccdULL;
        h2 ^= h2 >> 33;
        h2 *= 0xc4ceb9fe1a85ec53ULL;
        h2 ^= h2 >> 33;
        fp_type fp = static_cast<fp_type>(h2 & fp_mask);
        if (fp == 0) fp = 1;  // Reserve 0 as empty

        uint32_t sl = static_cast<uint32_t>(segment_length_);
        uint32_t sl_mask = sl - 1;

        // segment_count_length = segment_count_ * segment_length_.
        // Map top 32 bits of h into [0, scl) via multiply-shift, then
        // align to a segment boundary so the three positions land in
        // three consecutive segments.
        uint64_t scl = static_cast<uint64_t>(segment_count_) * segment_length_;
        uint32_t h_top = static_cast<uint32_t>(h >> 32);
        uint32_t h_base = static_cast<uint32_t>(
            (static_cast<uint64_t>(h_top) * scl) >> 32);
        if (h_base >= scl) h_base = static_cast<uint32_t>(scl - 1);
        h_base &= ~sl_mask;

        // Per-index within-segment offsets from different bit ranges of h.
        uint32_t off_a = static_cast<uint32_t>(h) & sl_mask;
        uint32_t off_b = static_cast<uint32_t>(h >> 18) & sl_mask;
        uint32_t off_c = static_cast<uint32_t>(h >> 36) & sl_mask;

        positions p{};
        p.h0 = h_base + 0 * sl + off_a;
        p.h1 = h_base + 1 * sl + off_b;
        p.h2 = h_base + 2 * sl + off_c;
        p.fingerprint = fp;
        return p;
    }

public:
    binary_fuse_filter() = default;

    bool build(const std::vector<std::string>& keys) {
        if (keys.empty()) return false;
        size_t n = keys.size();

        segment_length_ = calc_segment_length(n);
        double sf = calc_size_factor(n);
        size_t capacity = static_cast<size_t>(std::ceil(sf * static_cast<double>(n)));
        segment_count_ = (capacity + segment_length_ - 1) / segment_length_;
        if (segment_count_ < 1) segment_count_ = 1;
        // Add two extra segments on the right for the sliding window.
        array_length_ = (segment_count_ + 2) * segment_length_;

        std::mt19937_64 rng{42};

        for (int attempt = 0; attempt < 100; ++attempt) {
            seed_ = rng();
            table_.assign(array_length_, 0);

            // Degree / XOR tracking for peeling.
            std::vector<int32_t> degree(array_length_, 0);
            std::vector<uint64_t> deg_xor(array_length_, 0);
            std::vector<positions> hs(n);

            for (size_t i = 0; i < n; ++i) {
                hs[i] = compute(keys[i]);
                degree[hs[i].h0]++;
                degree[hs[i].h1]++;
                degree[hs[i].h2]++;
                deg_xor[hs[i].h0] ^= i;
                deg_xor[hs[i].h1] ^= i;
                deg_xor[hs[i].h2] ^= i;
            }

            struct peel_entry { size_t key_idx; size_t slot; };
            std::vector<peel_entry> stack;
            stack.reserve(n);

            std::vector<size_t> queue;
            queue.reserve(array_length_);
            for (size_t i = 0; i < array_length_; ++i) {
                if (degree[i] == 1) queue.push_back(i);
            }

            while (!queue.empty()) {
                size_t pos = queue.back();
                queue.pop_back();
                if (degree[pos] != 1) continue;

                size_t ki = static_cast<size_t>(deg_xor[pos]);
                if (ki >= n) continue;

                stack.push_back({ki, pos});

                const auto& kh = hs[ki];
                for (size_t slot : {static_cast<size_t>(kh.h0),
                                    static_cast<size_t>(kh.h1),
                                    static_cast<size_t>(kh.h2)}) {
                    degree[slot]--;
                    deg_xor[slot] ^= ki;
                    if (degree[slot] == 1) queue.push_back(slot);
                }
            }

            if (stack.size() != n) continue;  // Peeling failed

            for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
                const auto& kh = hs[it->key_idx];
                fp_type v = static_cast<fp_type>(
                    kh.fingerprint ^ table_[kh.h0] ^ table_[kh.h1] ^ table_[kh.h2]);
                table_[it->slot] = v;
            }
            return true;
        }
        return false;
    }

    [[nodiscard]] bool verify(std::string_view key) const noexcept {
        if (table_.empty()) return false;
        auto p = compute(key);
        return static_cast<fp_type>(table_[p.h0] ^ table_[p.h1] ^ table_[p.h2])
             == p.fingerprint;
    }

    [[nodiscard]] double bits_per_key(size_t key_count) const noexcept {
        if (key_count == 0) return 0.0;
        return static_cast<double>(table_.size() * FingerprintBits)
             / static_cast<double>(key_count);
    }

    [[nodiscard]] size_t memory_bytes() const noexcept {
        return table_.size() * sizeof(fp_type);
    }

    [[nodiscard]] size_t segment_length() const noexcept { return segment_length_; }
    [[nodiscard]] size_t segment_count() const noexcept { return segment_count_; }

    [[nodiscard]] std::vector<std::byte> serialize() const {
        std::vector<std::byte> out;
        phf_serial::append(out, static_cast<uint32_t>(FingerprintBits));
        phf_serial::append(out, seed_);
        phf_serial::append(out, static_cast<uint64_t>(segment_length_));
        phf_serial::append(out, static_cast<uint64_t>(segment_count_));
        phf_serial::append(out, static_cast<uint64_t>(array_length_));
        phf_serial::append_vector(out, table_);
        return out;
    }

    [[nodiscard]] static std::optional<binary_fuse_filter>
    deserialize(std::span<const std::byte> bytes) {
        phf_serial::reader r{bytes};
        uint32_t fp_bits{};
        uint64_t seed{}, sl{}, sc{}, al{};
        if (!r.read(fp_bits) || fp_bits != FingerprintBits) return std::nullopt;
        if (!r.read(seed) || !r.read(sl) || !r.read(sc) || !r.read(al)) {
            return std::nullopt;
        }

        binary_fuse_filter out;
        out.seed_ = seed;
        out.segment_length_ = static_cast<size_t>(sl);
        out.segment_count_ = static_cast<size_t>(sc);
        out.array_length_ = static_cast<size_t>(al);
        if (!r.read_vector(out.table_)) return std::nullopt;
        return out;
    }
};

} // namespace maph
