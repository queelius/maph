/**
 * @file ribbon_retrieval.hpp
 * @brief Homogeneous ribbon retrieval: M-bit values via banded GF(2) linear system.
 *
 * Generalizes ribbon_filter from stored fingerprints to user-provided
 * values. Where ribbon_filter stores hash(key) per row and verifies on
 * query, ribbon_retrieval stores v(key) per row and returns it on query.
 * The underlying machinery is identical: a banded matrix with bandwidth
 * w=64 solved by forward Gaussian elimination and back-substitution.
 *
 * Satisfies: retrieval<ribbon_retrieval<M>>
 *
 * Space:  ~1.04 * M bits per key (num_rows around 1.04 * num_keys, one
 *         value_type per row).
 * Query:  3 to ~32 XOR operations over solution entries inside a 64-slot
 *         window.
 * Build:  O(n * w) with w=64 for banded elimination. Retries on failure
 *         (the sparse system is solvable with high probability).
 *
 * Why this matters for cipher maps: the query path is a pure linear
 * function of stored state over GF(2)^M. For keys not in S, the output
 * is the XOR of "random" rows from the attacker's perspective: it is
 * indistinguishable from a legitimate lookup when values are themselves
 * pseudorandom. No membership signal leaks.
 */

#pragma once

#include "../concepts/retrieval.hpp"
#include "../core.hpp"
#include "../detail/fingerprint_hash.hpp"
#include "../detail/packed_value_array.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace maph {

template <unsigned M>
    requires (M >= 1 && M <= 64)
class ribbon_retrieval {
public:
    using packed_type = detail::packed_value_array<M>;
    using value_type = typename packed_type::value_type;

    static constexpr unsigned value_bits_v = M;

private:
    static constexpr uint64_t value_mask_ =
        (M == 64) ? ~uint64_t{0} : ((uint64_t{1} << M) - 1);
    static constexpr size_t W = 64;  // Band width = machine word size

    std::vector<value_type> solution_{};
    size_t num_rows_{0};
    size_t num_keys_{0};
    uint64_t seed_{0};

    struct row {
        size_t start;
        uint64_t coeffs;
        value_type value;
    };

    // Computed from key alone: deterministic given (key, seed, num_rows).
    // Used both at build and at query.
    std::pair<size_t, uint64_t> row_spec_for(std::string_view key) const noexcept {
        uint64_t h = membership_fingerprint(key) ^ seed_;
        size_t start = 0;
        if (num_rows_ > W) {
            start = static_cast<size_t>((h >> 32) % (num_rows_ - W + 1));
        }
        uint64_t c = h * 0xbf58476d1ce4e5b9ULL;
        c ^= c >> 31;
        c |= 1ULL;
        return {start, c};
    }

    value_type query_row(size_t base, uint64_t coeffs) const noexcept {
        value_type result = 0;
        uint64_t c = coeffs;
        while (c != 0) {
            size_t bit = static_cast<size_t>(std::countr_zero(c));
            result ^= solution_[base + bit];
            c &= c - 1;
        }
        return result;
    }

public:
    ribbon_retrieval() = default;

    [[nodiscard]] value_type lookup(std::string_view key) const noexcept {
        if (solution_.empty()) return value_type{0};
        auto [start, coeffs] = row_spec_for(key);
        return query_row(start, coeffs);
    }

    [[nodiscard]] size_t num_keys() const noexcept { return num_keys_; }
    [[nodiscard]] size_t value_bits() const noexcept { return M; }

    [[nodiscard]] double bits_per_key() const noexcept {
        if (num_keys_ == 0) return 0.0;
        return static_cast<double>(num_rows_) * static_cast<double>(M)
             / static_cast<double>(num_keys_);
    }

    [[nodiscard]] size_t memory_bytes() const noexcept {
        return solution_.size() * sizeof(value_type);
    }

    [[nodiscard]] size_t num_rows() const noexcept { return num_rows_; }
    [[nodiscard]] uint64_t seed() const noexcept { return seed_; }

    [[nodiscard]] std::vector<std::byte> serialize() const {
        std::vector<std::byte> out;
        auto append = [&](const auto& v) {
            auto b = std::bit_cast<std::array<std::byte, sizeof(v)>>(v);
            out.insert(out.end(), b.begin(), b.end());
        };
        append(static_cast<uint32_t>(M));
        append(seed_);
        append(static_cast<uint64_t>(num_rows_));
        append(static_cast<uint64_t>(num_keys_));
        append(static_cast<uint64_t>(solution_.size()));
        for (auto v : solution_) append(v);
        return out;
    }

    [[nodiscard]] static result<ribbon_retrieval>
    deserialize(std::span<const std::byte> bytes) {
        size_t off = 0;
        auto read = [&](auto& v) -> bool {
            if (off + sizeof(v) > bytes.size()) return false;
            std::memcpy(&v, bytes.data() + off, sizeof(v));
            off += sizeof(v);
            return true;
        };
        uint32_t width{};
        uint64_t seed{}, nrows{}, nkeys{}, sol_sz{};
        if (!read(width) || width != M) return std::unexpected(error::invalid_format);
        if (!read(seed) || !read(nrows) || !read(nkeys) || !read(sol_sz)) {
            return std::unexpected(error::invalid_format);
        }
        if (sol_sz > (bytes.size() - off) / sizeof(value_type)) {
            return std::unexpected(error::invalid_format);
        }
        ribbon_retrieval r;
        r.seed_ = seed;
        r.num_rows_ = static_cast<size_t>(nrows);
        r.num_keys_ = static_cast<size_t>(nkeys);
        r.solution_.resize(static_cast<size_t>(sol_sz));
        for (auto& v : r.solution_) {
            if (!read(v)) return std::unexpected(error::invalid_format);
        }
        return r;
    }

    // ===== Builder =====

    class builder {
        std::vector<std::pair<std::string, value_type>> pairs_{};
        uint64_t seed_{42};
        double epsilon_{0.08};
        size_t max_attempts_{50};

    public:
        builder() = default;

        builder& add(std::string_view key, value_type value) {
            pairs_.emplace_back(std::string{key},
                static_cast<value_type>(value & value_mask_));
            return *this;
        }

        builder& add_all(std::span<const std::string> keys,
                         std::span<const value_type> values) {
            size_t n = keys.size() < values.size() ? keys.size() : values.size();
            pairs_.reserve(pairs_.size() + n);
            for (size_t i = 0; i < n; ++i) {
                pairs_.emplace_back(keys[i],
                    static_cast<value_type>(values[i] & value_mask_));
            }
            return *this;
        }

        template <typename ValueFn>
            requires std::invocable<ValueFn, std::string_view>
        builder& add_all_with(std::span<const std::string> keys, ValueFn fn) {
            pairs_.reserve(pairs_.size() + keys.size());
            for (const auto& k : keys) {
                value_type v = static_cast<value_type>(
                    static_cast<uint64_t>(fn(std::string_view{k})) & value_mask_);
                pairs_.emplace_back(k, v);
            }
            return *this;
        }

        builder& with_seed(uint64_t s) { seed_ = s; return *this; }
        builder& with_epsilon(double e) { epsilon_ = e; return *this; }
        builder& with_max_attempts(size_t a) { max_attempts_ = a; return *this; }

        [[nodiscard]] result<ribbon_retrieval> build() {
            if (pairs_.empty()) return std::unexpected(error::optimization_failed);

            size_t n = pairs_.size();
            std::mt19937_64 rng{seed_};

            for (size_t attempt = 0; attempt < max_attempts_; ++attempt) {
                ribbon_retrieval out;
                out.seed_ = rng();
                out.num_keys_ = n;
                size_t extra = std::max(W, static_cast<size_t>(
                    static_cast<double>(n) * epsilon_));
                out.num_rows_ = n + extra;

                std::vector<row> rows;
                rows.reserve(n);
                for (const auto& [k, v] : pairs_) {
                    auto [start, coeffs] = out.row_spec_for(k);
                    rows.push_back(row{start, coeffs, v});
                }
                std::sort(rows.begin(), rows.end(),
                    [](const row& a, const row& b){ return a.start < b.start; });

                std::vector<uint64_t> pivot_coeffs(out.num_rows_, 0);
                std::vector<value_type> pivot_value(out.num_rows_, 0);
                bool ok = true;

                for (auto& r : rows) {
                    uint64_t c = r.coeffs;
                    value_type v = r.value;
                    size_t base = r.start;
                    while (c != 0) {
                        size_t bit = static_cast<size_t>(std::countr_zero(c));
                        size_t col = base + bit;
                        if (col >= out.num_rows_) { ok = false; break; }

                        if (pivot_coeffs[col] == 0) {
                            pivot_coeffs[col] = c >> bit;
                            pivot_value[col] = v;
                            c = 0;
                        } else {
                            c ^= pivot_coeffs[col] << bit;
                            v = static_cast<value_type>(v ^ pivot_value[col]);
                        }
                    }
                    if (!ok) break;
                }
                if (!ok) continue;

                out.solution_.assign(out.num_rows_, 0);
                for (size_t col = out.num_rows_; col-- > 0;) {
                    if (pivot_coeffs[col] == 0) continue;
                    uint64_t c = pivot_coeffs[col];
                    value_type val = pivot_value[col];
                    uint64_t rest = c >> 1;
                    size_t offset = 1;
                    while (rest != 0) {
                        size_t bit = static_cast<size_t>(std::countr_zero(rest));
                        size_t ref = col + offset + bit;
                        if (ref < out.num_rows_) {
                            val = static_cast<value_type>(val ^ out.solution_[ref]);
                        }
                        rest >>= (bit + 1);
                        offset += bit + 1;
                    }
                    out.solution_[col] = val;
                }

                bool verified = true;
                for (const auto& [k, v] : pairs_) {
                    auto [start, coeffs] = out.row_spec_for(k);
                    if (out.query_row(start, coeffs) != v) { verified = false; break; }
                }
                if (verified) return out;
            }
            return std::unexpected(error::optimization_failed);
        }
    };
};

} // namespace maph
