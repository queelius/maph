/**
 * @file membership.hpp
 * @brief Membership verification strategies for perfect hash functions
 *
 * Provides multiple strategies for verifying that a queried key belongs
 * to the original build set. Each strategy offers a different tradeoff
 * between space, query speed, and false positive rate.
 */

#pragma once

#include "core.hpp"
#include <vector>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <optional>
#include <functional>
#include <random>
#include <algorithm>
#include <bit>
#include <cmath>

namespace maph {

// ===== COMMON FINGERPRINT HASH =====

// Independent hash for fingerprinting (must differ from perfect hash internals).
// SplitMix64 finalization applied to FNV-1a.
inline uint64_t membership_fingerprint(std::string_view key) noexcept {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (unsigned char c : key) {
        h ^= c;
        h *= 0x100000001b3ULL;
    }
    h ^= h >> 30;
    h *= 0xbf58476d1ce4e5b9ULL;
    h ^= h >> 27;
    h *= 0x94d049bb133111ebULL;
    h ^= h >> 31;
    return h;
}

// ===== STRATEGY 1: COMPACT PACKED FINGERPRINT ARRAY =====

/**
 * @class packed_fingerprint_array
 * @brief Stores k-bit fingerprints in a tightly packed bit array
 *
 * Space: exactly FingerprintBits bits per key.
 * Query: extract k-bit fingerprint from packed array, compare.
 * FP rate: 2^-FingerprintBits.
 *
 * @tparam FingerprintBits Width of each fingerprint (8, 16, or 32)
 */
template<unsigned FingerprintBits>
    requires (FingerprintBits >= 1 && FingerprintBits <= 32)
class packed_fingerprint_array {
    static constexpr uint64_t fp_mask = (1ULL << FingerprintBits) - 1;

    std::vector<uint64_t> data_;
    size_t num_slots_{0};

    static uint64_t truncate_fp(std::string_view key) noexcept {
        return membership_fingerprint(key) & fp_mask;
    }

    uint64_t extract(size_t slot) const noexcept {
        size_t bit_pos = slot * FingerprintBits;
        size_t word_idx = bit_pos / 64;
        size_t bit_offset = bit_pos % 64;

        uint64_t val = data_[word_idx] >> bit_offset;
        if (bit_offset + FingerprintBits > 64 && word_idx + 1 < data_.size()) {
            val |= data_[word_idx + 1] << (64 - bit_offset);
        }
        return val & fp_mask;
    }

    void store(size_t slot, uint64_t value) noexcept {
        size_t bit_pos = slot * FingerprintBits;
        size_t word_idx = bit_pos / 64;
        size_t bit_offset = bit_pos % 64;

        data_[word_idx] &= ~(fp_mask << bit_offset);
        data_[word_idx] |= (value & fp_mask) << bit_offset;

        if (bit_offset + FingerprintBits > 64 && word_idx + 1 < data_.size()) {
            size_t bits_in_first = 64 - bit_offset;
            size_t bits_in_second = FingerprintBits - bits_in_first;
            uint64_t mask2 = (1ULL << bits_in_second) - 1;
            data_[word_idx + 1] &= ~mask2;
            data_[word_idx + 1] |= (value >> bits_in_first) & mask2;
        }
    }

public:
    packed_fingerprint_array() = default;

    void build(const std::vector<std::string>& keys,
               std::function<std::optional<size_t>(std::string_view)> slot_for,
               size_t total_slots) {
        num_slots_ = total_slots;
        size_t total_bits = num_slots_ * FingerprintBits;
        data_.assign((total_bits + 63) / 64, 0);

        for (const auto& key : keys) {
            if (auto slot = slot_for(key)) {
                store(*slot, truncate_fp(key));
            }
        }
    }

    [[nodiscard]] bool verify(std::string_view key, size_t slot) const noexcept {
        if (slot >= num_slots_) return false;
        return extract(slot) == truncate_fp(key);
    }

    [[nodiscard]] double bits_per_key(size_t key_count) const noexcept {
        return key_count > 0 ? static_cast<double>(FingerprintBits) : 0.0;
    }

    [[nodiscard]] size_t memory_bytes() const noexcept {
        return data_.size() * sizeof(uint64_t);
    }

    [[nodiscard]] std::vector<std::byte> serialize() const {
        std::vector<std::byte> out;
        auto append = [&](const auto& val) {
            auto bytes = std::bit_cast<std::array<std::byte, sizeof(val)>>(val);
            out.insert(out.end(), bytes.begin(), bytes.end());
        };
        append(static_cast<uint32_t>(FingerprintBits));
        append(static_cast<uint64_t>(num_slots_));
        append(static_cast<uint64_t>(data_.size()));
        for (auto w : data_) append(w);
        return out;
    }

    [[nodiscard]] static std::optional<packed_fingerprint_array> deserialize(std::span<const std::byte> bytes) {
        size_t off = 0;
        auto read = [&](auto& val) -> bool {
            if (off + sizeof(val) > bytes.size()) return false;
            std::memcpy(&val, bytes.data() + off, sizeof(val));
            off += sizeof(val);
            return true;
        };
        uint32_t fp_bits{}; uint64_t slots{}; uint64_t words{};
        if (!read(fp_bits) || fp_bits != FingerprintBits) return std::nullopt;
        if (!read(slots) || !read(words)) return std::nullopt;
        if (words > (bytes.size() - off) / sizeof(uint64_t)) return std::nullopt;

        packed_fingerprint_array r;
        r.num_slots_ = static_cast<size_t>(slots);
        r.data_.resize(static_cast<size_t>(words));
        for (auto& w : r.data_) { if (!read(w)) return std::nullopt; }
        return r;
    }
};

// ===== STRATEGY 2: XOR FILTER =====

/**
 * @class xor_filter
 * @brief 3-wise xor filter for membership testing
 *
 * Space: ~1.23 * FingerprintBits bits per key.
 * Query: 3 memory accesses + XOR + compare.
 * FP rate: 2^-FingerprintBits.
 *
 * Construction uses the "peeling" algorithm on a 3-partite hypergraph.
 * Retries with a new seed if peeling fails (~3% per attempt).
 *
 * @tparam FingerprintBits Width of each fingerprint (8, 16, or 32)
 */
template<unsigned FingerprintBits>
    requires (FingerprintBits == 8 || FingerprintBits == 16 || FingerprintBits == 32)
class xor_filter {
    using fp_type = std::conditional_t<FingerprintBits <= 8, uint8_t,
                    std::conditional_t<FingerprintBits <= 16, uint16_t, uint32_t>>;

    static constexpr uint64_t fp_mask = (1ULL << FingerprintBits) - 1;

    std::vector<fp_type> table_;
    size_t segment_size_{0};
    uint64_t seed_{0};

    struct key_hashes {
        size_t h0, h1, h2;
        fp_type fingerprint;
    };

    key_hashes hash_key(std::string_view key) const noexcept {
        uint64_t h = membership_fingerprint(key) ^ seed_;

        // Second independent hash via additional mixing
        uint64_t h2 = h;
        h2 ^= h2 >> 33;
        h2 *= 0xff51afd7ed558ccdULL;
        h2 ^= h2 >> 33;
        h2 *= 0xc4ceb9fe1a85ec53ULL;
        h2 ^= h2 >> 33;

        auto fp = static_cast<fp_type>(h2 & fp_mask);
        if (fp == 0) fp = 1;  // Non-zero fingerprint for correctness

        return {
            static_cast<size_t>(h % segment_size_),
            static_cast<size_t>((h >> 21) % segment_size_) + segment_size_,
            static_cast<size_t>((h2 >> 11) % segment_size_) + 2 * segment_size_,
            fp
        };
    }

public:
    xor_filter() = default;

    bool build(const std::vector<std::string>& keys) {
        if (keys.empty()) return false;

        size_t n = keys.size();
        segment_size_ = std::max(size_t{4}, static_cast<size_t>(std::ceil(n * 1.23 / 3.0)));
        size_t table_size = 3 * segment_size_;

        std::mt19937_64 rng{42};

        for (int attempt = 0; attempt < 100; ++attempt) {
            seed_ = rng();
            table_.assign(table_size, 0);

            // Compute hashes and build degree/xor tracking
            std::vector<int32_t> degree(table_size, 0);
            std::vector<uint64_t> deg_xor(table_size, 0);
            std::vector<key_hashes> hashes(n);

            for (size_t i = 0; i < n; ++i) {
                hashes[i] = hash_key(keys[i]);
                degree[hashes[i].h0]++;
                degree[hashes[i].h1]++;
                degree[hashes[i].h2]++;
                deg_xor[hashes[i].h0] ^= i;
                deg_xor[hashes[i].h1] ^= i;
                deg_xor[hashes[i].h2] ^= i;
            }

            // Peeling: repeatedly remove degree-1 slots
            struct peel_entry { size_t key_idx; size_t slot; };
            std::vector<peel_entry> stack;
            stack.reserve(n);

            std::vector<size_t> queue;
            for (size_t i = 0; i < table_size; ++i) {
                if (degree[i] == 1) queue.push_back(i);
            }

            while (!queue.empty()) {
                size_t pos = queue.back();
                queue.pop_back();
                if (degree[pos] != 1) continue;

                size_t ki = static_cast<size_t>(deg_xor[pos]);
                if (ki >= n) continue;

                stack.push_back({ki, pos});

                const auto& kh = hashes[ki];
                for (size_t slot : {kh.h0, kh.h1, kh.h2}) {
                    degree[slot]--;
                    deg_xor[slot] ^= ki;
                    if (degree[slot] == 1) queue.push_back(slot);
                }
            }

            if (stack.size() != n) continue;  // Peeling failed, retry

            // Assign values in reverse peel order
            for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
                const auto& kh = hashes[it->key_idx];
                fp_type val = kh.fingerprint ^ table_[kh.h0] ^ table_[kh.h1] ^ table_[kh.h2];
                table_[it->slot] = val;
            }
            return true;
        }
        return false;
    }

    [[nodiscard]] bool verify(std::string_view key) const noexcept {
        if (table_.empty()) return false;
        auto kh = hash_key(key);
        return (table_[kh.h0] ^ table_[kh.h1] ^ table_[kh.h2]) == kh.fingerprint;
    }

    [[nodiscard]] double bits_per_key(size_t key_count) const noexcept {
        return key_count > 0 ? static_cast<double>(table_.size() * FingerprintBits) / key_count : 0.0;
    }

    [[nodiscard]] size_t memory_bytes() const noexcept {
        return table_.size() * sizeof(fp_type);
    }

    [[nodiscard]] std::vector<std::byte> serialize() const {
        std::vector<std::byte> out;
        auto append = [&](const auto& val) {
            auto bytes = std::bit_cast<std::array<std::byte, sizeof(val)>>(val);
            out.insert(out.end(), bytes.begin(), bytes.end());
        };
        append(static_cast<uint32_t>(FingerprintBits));
        append(seed_);
        append(static_cast<uint64_t>(segment_size_));
        append(static_cast<uint64_t>(table_.size()));
        for (auto v : table_) append(v);
        return out;
    }

    [[nodiscard]] static std::optional<xor_filter> deserialize(std::span<const std::byte> bytes) {
        size_t off = 0;
        auto read = [&](auto& val) -> bool {
            if (off + sizeof(val) > bytes.size()) return false;
            std::memcpy(&val, bytes.data() + off, sizeof(val));
            off += sizeof(val);
            return true;
        };
        uint32_t fp_bits{}; uint64_t seed{}, seg{}, tsize{};
        if (!read(fp_bits) || fp_bits != FingerprintBits) return std::nullopt;
        if (!read(seed) || !read(seg) || !read(tsize)) return std::nullopt;
        if (tsize > (bytes.size() - off) / sizeof(fp_type)) return std::nullopt;

        xor_filter r;
        r.seed_ = seed;
        r.segment_size_ = static_cast<size_t>(seg);
        r.table_.resize(static_cast<size_t>(tsize));
        for (auto& v : r.table_) { if (!read(v)) return std::nullopt; }
        return r;
    }
};

// ===== STRATEGY 3: RIBBON RETRIEVAL =====

/**
 * @class ribbon_filter
 * @brief Homogeneous ribbon retrieval for membership testing
 *
 * Space: ~FingerprintBits * (1 + epsilon) bits per key (epsilon ~3-8%).
 * Query: hash to starting row + XOR chain over ~64 solution entries.
 * FP rate: 2^-FingerprintBits.
 *
 * Uses a banded matrix with bandwidth w=64. Construction: sort rows by
 * start position, forward Gaussian elimination, back-substitution.
 *
 * @tparam FingerprintBits Width of stored fingerprint (8, 16, or 32)
 */
template<unsigned FingerprintBits>
    requires (FingerprintBits == 8 || FingerprintBits == 16 || FingerprintBits == 32)
class ribbon_filter {
    using fp_type = std::conditional_t<FingerprintBits <= 8, uint8_t,
                    std::conditional_t<FingerprintBits <= 16, uint16_t, uint32_t>>;

    static constexpr uint64_t fp_mask = (1ULL << FingerprintBits) - 1;
    static constexpr size_t W = 64;  // Band width = machine word

    std::vector<fp_type> solution_;  // One fp_type per row
    size_t num_rows_{0};
    uint64_t seed_{0};

    // Each key produces a row: starting position, 64-bit coefficients, desired result
    struct row {
        size_t start;
        uint64_t coeffs;  // Bit i means column (start + i) participates
        fp_type result;
    };

    row make_row(std::string_view key) const noexcept {
        uint64_t h = membership_fingerprint(key) ^ seed_;

        size_t start = 0;
        if (num_rows_ > W) {
            start = static_cast<size_t>((h >> 32) % (num_rows_ - W + 1));
        }

        // Coefficients from a second mix
        uint64_t c = h * 0xbf58476d1ce4e5b9ULL;
        c ^= c >> 31;
        c |= 1ULL;  // Ensure at least bit 0 is set (non-zero row)

        // Fingerprint from a third mix
        uint64_t fh = h * 0x9e3779b97f4a7c15ULL;
        fh ^= fh >> 30;
        auto fp = static_cast<fp_type>(fh & fp_mask);

        return {start, c, fp};
    }

    // Query: compute XOR of solution[start+i] for each set bit i in coeffs
    fp_type query_row(const row& r) const noexcept {
        fp_type result = 0;
        uint64_t c = r.coeffs;
        size_t base = r.start;
        while (c != 0) {
            size_t bit = static_cast<size_t>(std::countr_zero(c));  // Lowest set bit
            result ^= solution_[base + bit];
            c &= c - 1;  // Clear lowest set bit
        }
        return result;
    }

public:
    ribbon_filter() = default;

    bool build(const std::vector<std::string>& keys) {
        if (keys.empty()) return false;
        size_t n = keys.size();
        std::mt19937_64 rng{42};

        for (int attempt = 0; attempt < 50; ++attempt) {
            seed_ = rng();
            num_rows_ = n + std::max(size_t{W}, static_cast<size_t>(n * 0.08));

            // Build rows, sorted by start position
            std::vector<row> rows(n);
            for (size_t i = 0; i < n; ++i) {
                rows[i] = make_row(keys[i]);
            }
            std::sort(rows.begin(), rows.end(),
                      [](const row& a, const row& b) { return a.start < b.start; });

            // Forward elimination.
            // pivot_coeffs[col] and pivot_result[col] store the pivot row
            // that "owns" column col.
            std::vector<uint64_t> pivot_coeffs(num_rows_, 0);
            std::vector<fp_type> pivot_result(num_rows_, 0);
            bool ok = true;

            for (auto& r : rows) {
                uint64_t c = r.coeffs;
                fp_type res = r.result;
                size_t base = r.start;

                while (c != 0) {
                    size_t bit = static_cast<size_t>(std::countr_zero(c));
                    size_t col = base + bit;

                    if (col >= num_rows_) { ok = false; break; }

                    if (pivot_coeffs[col] == 0) {
                        // Claim this column. Shift so leading coeff aligns with bit 0.
                        pivot_coeffs[col] = c >> bit;
                        pivot_result[col] = res;
                        c = 0;  // Row consumed
                    } else {
                        // Eliminate: XOR with existing pivot, re-align
                        c ^= pivot_coeffs[col] << bit;
                        res ^= pivot_result[col];
                    }
                }
                if (!ok) break;
            }

            if (!ok) continue;

            // Back-substitution
            solution_.assign(num_rows_, 0);
            for (size_t col = num_rows_; col-- > 0;) {
                if (pivot_coeffs[col] == 0) continue;  // Free variable

                uint64_t c = pivot_coeffs[col];
                fp_type val = pivot_result[col];

                // XOR in already-solved columns.
                // Bit 0 of c is the pivot column itself; higher bits reference
                // col+1, col+2, etc.
                uint64_t rest = c >> 1;
                size_t offset = 1;
                while (rest != 0) {
                    size_t bit = static_cast<size_t>(std::countr_zero(rest));
                    size_t ref = col + offset + bit;
                    if (ref < num_rows_) {
                        val ^= solution_[ref];
                    }
                    rest >>= (bit + 1);
                    offset += bit + 1;
                }
                solution_[col] = val;
            }

            // Verify all keys
            bool verified = true;
            for (size_t i = 0; i < n; ++i) {
                auto r = make_row(keys[i]);
                if (query_row(r) != r.result) { verified = false; break; }
            }
            if (verified) return true;
        }
        return false;
    }

    [[nodiscard]] bool verify(std::string_view key) const noexcept {
        if (solution_.empty()) return false;
        auto r = make_row(key);
        return query_row(r) == r.result;
    }

    [[nodiscard]] double bits_per_key(size_t key_count) const noexcept {
        return key_count > 0 ? static_cast<double>(solution_.size() * FingerprintBits) / key_count : 0.0;
    }

    [[nodiscard]] size_t memory_bytes() const noexcept {
        return solution_.size() * sizeof(fp_type);
    }

    [[nodiscard]] std::vector<std::byte> serialize() const {
        std::vector<std::byte> out;
        auto append = [&](const auto& val) {
            auto bytes = std::bit_cast<std::array<std::byte, sizeof(val)>>(val);
            out.insert(out.end(), bytes.begin(), bytes.end());
        };
        append(static_cast<uint32_t>(FingerprintBits));
        append(seed_);
        append(static_cast<uint64_t>(num_rows_));
        append(static_cast<uint64_t>(solution_.size()));
        for (auto v : solution_) append(v);
        return out;
    }

    [[nodiscard]] static std::optional<ribbon_filter> deserialize(std::span<const std::byte> bytes) {
        size_t off = 0;
        auto read = [&](auto& val) -> bool {
            if (off + sizeof(val) > bytes.size()) return false;
            std::memcpy(&val, bytes.data() + off, sizeof(val));
            off += sizeof(val);
            return true;
        };
        uint32_t fp_bits{}; uint64_t seed{}, nrows{}, sol_size{};
        if (!read(fp_bits) || fp_bits != FingerprintBits) return std::nullopt;
        if (!read(seed) || !read(nrows) || !read(sol_size)) return std::nullopt;
        if (sol_size > (bytes.size() - off) / sizeof(fp_type)) return std::nullopt;

        ribbon_filter r;
        r.seed_ = seed;
        r.num_rows_ = static_cast<size_t>(nrows);
        r.solution_.resize(static_cast<size_t>(sol_size));
        for (auto& v : r.solution_) { if (!read(v)) return std::nullopt; }
        return r;
    }
};

// ===== STRATEGY 4: CONFIGURABLE FINGERPRINT VERIFIER =====

/**
 * @class fingerprint_verifier
 * @brief Policy wrapper with configurable fingerprint width
 *
 * Wraps packed_fingerprint_array. Set FingerprintBits=0 to disable
 * verification entirely. This is the integration interface for
 * production perfect hashers.
 *
 * @tparam FingerprintBits 0 (disabled), 8, 16, or 32
 */
template<unsigned FingerprintBits>
    requires (FingerprintBits == 0 || FingerprintBits == 8 || FingerprintBits == 16 || FingerprintBits == 32)
class fingerprint_verifier {
    packed_fingerprint_array<FingerprintBits> inner_;

public:
    void build(const std::vector<std::string>& keys,
               std::function<std::optional<size_t>(std::string_view)> slot_for,
               size_t total_slots) {
        inner_.build(keys, slot_for, total_slots);
    }

    [[nodiscard]] bool verify(std::string_view key, size_t slot) const noexcept {
        return inner_.verify(key, slot);
    }

    [[nodiscard]] double bits_per_key(size_t key_count) const noexcept {
        return inner_.bits_per_key(key_count);
    }

    [[nodiscard]] size_t memory_bytes() const noexcept {
        return inner_.memory_bytes();
    }

    [[nodiscard]] std::vector<std::byte> serialize() const { return inner_.serialize(); }

    [[nodiscard]] static std::optional<fingerprint_verifier> deserialize(std::span<const std::byte> bytes) {
        auto inner = packed_fingerprint_array<FingerprintBits>::deserialize(bytes);
        if (!inner) return std::nullopt;
        fingerprint_verifier r;
        r.inner_ = std::move(*inner);
        return r;
    }
};

// Specialization: disabled (0 bits)
template<>
class fingerprint_verifier<0> {
public:
    void build(const std::vector<std::string>&,
               std::function<std::optional<size_t>(std::string_view)>,
               size_t) {}

    [[nodiscard]] bool verify(std::string_view, size_t) const noexcept { return true; }
    [[nodiscard]] double bits_per_key(size_t) const noexcept { return 0.0; }
    [[nodiscard]] size_t memory_bytes() const noexcept { return 0; }
    [[nodiscard]] std::vector<std::byte> serialize() const { return {}; }

    [[nodiscard]] static std::optional<fingerprint_verifier> deserialize(std::span<const std::byte>) {
        return fingerprint_verifier{};
    }
};

} // namespace maph
