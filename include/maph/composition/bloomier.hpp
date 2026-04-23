/**
 * @file bloomier.hpp
 * @brief Generalized approximate function: retrieval gated by an oracle.
 *
 * bloomier<Retrieval, Oracle> composes any retrieval with any membership
 * oracle to form an "approximate function" lookup(k) -> optional<value_type>.
 * For keys in the build set S: lookup returns the stored value.
 * For keys not in S: the oracle rejects most with a false-positive rate
 * of its configured width, returning nullopt. For the small fraction
 * that slip through (FPR ~ 2^-fp_bits), lookup returns some value
 * drawn from the retrieval's GIGO semantics.
 *
 * Generalizes perfect_filter, which is the special case where
 * retrieval is a PHF-as-retrieval (slot index) and oracle is a
 * packed fingerprint array tied to the same PHF's slots.
 *
 * Compositional shape:
 *   - retrieval component decides *what* value to return given "in S"
 *   - oracle component decides *whether* the key is in S at all
 *   - they are independent: swap in ribbon, xor, binary_fuse, etc.
 *     for the oracle; swap in ribbon_retrieval, phf_value_array, etc.
 *     for the retrieval
 *
 * Space: retrieval.bits_per_key + oracle.bits_per_key
 * Query: oracle.verify cost + (on hit) retrieval.lookup cost
 * FPR:   oracle's FPR. On FP, caller gets some value (not the "right" one).
 *
 * This is explicitly the *approximate-map* path, distinct from the pure
 * retrieval path used by cipher maps. Cipher maps want GIGO (no None)
 * because an oracle leaks membership; approximate maps want None
 * because missing keys should be distinguishable.
 */

#pragma once

#include "../concepts/retrieval.hpp"
#include "../core.hpp"
#include "../detail/serialization.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace maph {

template <typename Retrieval, typename Oracle>
    requires retrieval<Retrieval>
class bloomier {
public:
    using retrieval_type = Retrieval;
    using oracle_type = Oracle;
    using value_type = typename Retrieval::value_type;

    bloomier() = default;

    bloomier(Retrieval r, Oracle o)
        : r_(std::move(r)), o_(std::move(o)) {}

    // The approximate-map query. Returns nullopt when the oracle
    // rejects; otherwise returns the retrieval's value (exact for
    // members, arbitrary for false-positive non-members).
    [[nodiscard]] std::optional<value_type> lookup(std::string_view key) const {
        if (!o_.verify(key)) return std::nullopt;
        return r_.lookup(key);
    }

    // Convenience: just the membership check, no value.
    [[nodiscard]] bool contains(std::string_view key) const {
        return o_.verify(key);
    }

    [[nodiscard]] size_t num_keys() const noexcept { return r_.num_keys(); }
    [[nodiscard]] size_t value_bits() const noexcept { return r_.value_bits(); }

    [[nodiscard]] double bits_per_key() const noexcept {
        double rb = r_.bits_per_key();
        double ob;
        if constexpr (requires(const Oracle& o) {
            o.bits_per_key(std::declval<size_t>());
        }) {
            ob = o_.bits_per_key(num_keys());
        } else if constexpr (requires(const Oracle& o) { o.bits_per_key(); }) {
            ob = o_.bits_per_key();
        } else {
            ob = static_cast<double>(o_.memory_bytes() * 8) /
                 static_cast<double>(num_keys() == 0 ? 1 : num_keys());
        }
        return rb + ob;
    }

    [[nodiscard]] size_t memory_bytes() const noexcept {
        return r_.memory_bytes() + o_.memory_bytes();
    }

    [[nodiscard]] const Retrieval& get_retrieval() const noexcept { return r_; }
    [[nodiscard]] const Oracle& get_oracle() const noexcept { return o_; }

    [[nodiscard]] std::vector<std::byte> serialize() const {
        std::vector<std::byte> out;
        auto r_bytes = r_.serialize();
        auto o_bytes = o_.serialize();
        phf_serial::append(out, static_cast<uint64_t>(r_bytes.size()));
        out.insert(out.end(), r_bytes.begin(), r_bytes.end());
        out.insert(out.end(), o_bytes.begin(), o_bytes.end());
        return out;
    }

    // ===== Builder =====

    class builder {
        typename Retrieval::builder rb_{};
        std::vector<std::string> keys_{};

    public:
        builder() = default;

        builder& add(std::string_view key, const value_type& v) {
            rb_.add(key, v);
            keys_.emplace_back(key);
            return *this;
        }

        builder& add_all(std::span<const std::string> keys,
                         std::span<const value_type> values) {
            size_t n = keys.size() < values.size() ? keys.size() : values.size();
            keys_.reserve(keys_.size() + n);
            for (size_t i = 0; i < n; ++i) {
                rb_.add(keys[i], values[i]);
                keys_.emplace_back(keys[i]);
            }
            return *this;
        }

        template <typename ValueFn>
            requires std::invocable<ValueFn, std::string_view>
        builder& add_all_with(std::span<const std::string> keys, ValueFn fn) {
            keys_.reserve(keys_.size() + keys.size());
            for (const auto& k : keys) {
                value_type v = static_cast<value_type>(fn(std::string_view{k}));
                rb_.add(k, v);
                keys_.emplace_back(k);
            }
            return *this;
        }

        // Forward retrieval builder knobs.
        builder& with_seed(uint64_t s)
            requires requires(typename Retrieval::builder& b) { b.with_seed(s); } {
            rb_.with_seed(s); return *this;
        }

        builder& with_threads(size_t n)
            requires requires(typename Retrieval::builder& b) { b.with_threads(n); } {
            rb_.with_threads(n); return *this;
        }

        [[nodiscard]] result<bloomier> build() {
            // Build retrieval first (takes keys + values).
            auto r = rb_.build();
            if (!r) return std::unexpected(r.error());

            // Build oracle on keys alone. Oracles expose a free-form
            // build(keys) method (xor_filter, ribbon_filter,
            // binary_fuse_filter all do this).
            Oracle o;
            if (!o.build(keys_)) return std::unexpected(error::optimization_failed);

            return bloomier{std::move(*r), std::move(o)};
        }
    };

private:
    Retrieval r_{};
    Oracle o_{};
};

} // namespace maph
