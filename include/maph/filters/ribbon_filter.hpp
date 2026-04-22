/**
 * @file ribbon_filter.hpp
 * @brief Homogeneous ribbon retrieval for approximate membership.
 *
 * Restored from git history (removed in commit d805e47).
 *
 * Space: ~FingerprintBits * (1 + epsilon) bits per key (epsilon ~3-8%).
 * Query: hash to starting row + XOR chain over ~64 solution entries.
 * FP rate: 2^-FingerprintBits.
 *
 * Uses a banded matrix with bandwidth w=64. Construction: sort rows by
 * start position, forward Gaussian elimination, back-substitution.
 */

#pragma once

#include "../core.hpp"
#include "../detail/fingerprint_hash.hpp"
#include <algorithm>
#include <array>
#include <bit>
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

/**
 * @class ribbon_filter
 * @brief Homogeneous ribbon retrieval for membership testing
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

} // namespace maph
