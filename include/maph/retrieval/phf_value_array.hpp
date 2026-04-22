/**
 * @file phf_value_array.hpp
 * @brief Retrieval via PHF + packed M-bit value array. Baseline implementation.
 *
 * For each key k in S with value v(k), this stores v(k) at slot
 * phf.slot_for(k). A lookup is (phf.slot_for, array read). Satisfies
 * the retrieval concept.
 *
 * Space:  bits_per_key(PHF) + M bits per key.
 * Query:  one PHF query + one packed-array read.
 * Build:  inherits the PHF's build; then one pass over pairs to fill
 *         the array.
 *
 * This is the *naive* retrieval baseline. It pays the PHF's full
 * bits/key (around 2.7 for PHOBIC) on top of the M-bit value payload.
 * Ribbon retrieval achieves ~1.04 * M bits/key in total by avoiding a
 * separate PHF, which wins decisively for small M. Keep this
 * implementation for comparisons and as a reference for GIGO semantics.
 *
 * GIGO semantics: for k not in S, lookup(k) returns whatever value
 * happens to be at phf.slot_for(k). No branch, no sentinel, no
 * distinguishable failure mode. When values are pseudorandom, the
 * output for non-members is indistinguishable from a real hit.
 */

#pragma once

#include "../concepts/perfect_hash_function.hpp"
#include "../concepts/retrieval.hpp"
#include "../core.hpp"
#include "../detail/packed_value_array.hpp"

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

template <perfect_hash_function PHF, unsigned M>
    requires (M >= 1 && M <= 64)
class phf_value_array {
public:
    using packed_type = detail::packed_value_array<M>;
    using value_type = typename packed_type::value_type;

    static constexpr unsigned value_bits_v = M;

    phf_value_array() = default;

    // ===== Retrieval query =====

    [[nodiscard]] value_type lookup(std::string_view key) const noexcept {
        return values_.get(static_cast<size_t>(phf_.slot_for(key)));
    }

    [[nodiscard]] size_t num_keys() const noexcept { return phf_.num_keys(); }
    [[nodiscard]] size_t value_bits() const noexcept { return M; }

    [[nodiscard]] double bits_per_key() const noexcept {
        if (phf_.num_keys() == 0) return 0.0;
        double phf_b = phf_.bits_per_key();
        double val_b = static_cast<double>(values_.memory_bytes()) * 8.0
                     / static_cast<double>(phf_.num_keys());
        return phf_b + val_b;
    }

    [[nodiscard]] size_t memory_bytes() const noexcept {
        return phf_.memory_bytes() + values_.memory_bytes();
    }

    [[nodiscard]] const PHF& phf() const noexcept { return phf_; }
    [[nodiscard]] const packed_type& values() const noexcept { return values_; }

    [[nodiscard]] std::vector<std::byte> serialize() const {
        auto phf_bytes = phf_.serialize();
        auto val_bytes = values_.serialize();
        std::vector<std::byte> out;
        out.reserve(8 + phf_bytes.size() + val_bytes.size());
        auto append_u64 = [&](uint64_t x) {
            auto b = std::bit_cast<std::array<std::byte, sizeof(x)>>(x);
            out.insert(out.end(), b.begin(), b.end());
        };
        append_u64(static_cast<uint64_t>(phf_bytes.size()));
        out.insert(out.end(), phf_bytes.begin(), phf_bytes.end());
        out.insert(out.end(), val_bytes.begin(), val_bytes.end());
        return out;
    }

    // ===== Builder =====

    class builder {
        typename PHF::builder phf_builder_{};
        std::vector<std::pair<std::string, value_type>> pairs_{};
        value_type fill_pattern_{0};  // Pattern written to unused slots

    public:
        builder() = default;

        builder& add(std::string_view key, value_type value) {
            phf_builder_.add(key);
            pairs_.emplace_back(std::string{key}, value);
            return *this;
        }

        // Parallel vectors. Must have equal length.
        builder& add_all(std::span<const std::string> keys,
                         std::span<const value_type> values) {
            size_t n = keys.size() < values.size() ? keys.size() : values.size();
            pairs_.reserve(pairs_.size() + n);
            for (size_t i = 0; i < n; ++i) {
                phf_builder_.add(keys[i]);
                pairs_.emplace_back(keys[i], values[i]);
            }
            return *this;
        }

        // Compute the value from the key. Convenient for fingerprint-style
        // retrievals and for tests that use identity-like value functions.
        template <typename ValueFn>
            requires std::invocable<ValueFn, std::string_view>
        builder& add_all_with(std::span<const std::string> keys, ValueFn fn) {
            pairs_.reserve(pairs_.size() + keys.size());
            for (const auto& k : keys) {
                value_type v = static_cast<value_type>(fn(std::string_view{k}));
                phf_builder_.add(k);
                pairs_.emplace_back(k, v);
            }
            return *this;
        }

        // Forward inner-PHF builder knobs when present. Opt-in via
        // requires-clauses so non-supporting PHFs still expose a uniform
        // builder interface.

        builder& with_seed(uint64_t seed)
            requires requires(typename PHF::builder& b) { b.with_seed(seed); } {
            phf_builder_.with_seed(seed);
            return *this;
        }

        builder& with_threads(size_t n)
            requires requires(typename PHF::builder& b) { b.with_threads(n); } {
            phf_builder_.with_threads(n);
            return *this;
        }

        // Pattern written to unused slots (range_size > num_keys for
        // non-minimal PHFs). For encoded_retrieval, set this to a pattern
        // that decodes to the codec's default value so non-members
        // landing in unused slots produce the intended default.
        builder& with_fill_pattern(value_type p) { fill_pattern_ = p; return *this; }

        [[nodiscard]] result<phf_value_array> build() {
            auto built = phf_builder_.build();
            if (!built.has_value()) return std::unexpected(built.error());

            phf_value_array out{};
            out.phf_ = std::move(*built);
            out.values_.resize(out.phf_.range_size());
            if (fill_pattern_ != 0) {
                for (size_t i = 0; i < out.phf_.range_size(); ++i) {
                    out.values_.set(i, fill_pattern_);
                }
            }

            for (const auto& [k, v] : pairs_) {
                auto slot = static_cast<size_t>(out.phf_.slot_for(k));
                out.values_.set(slot, v);
            }
            return out;
        }
    };

    // ===== Deserialize =====

    [[nodiscard]] static result<phf_value_array>
    deserialize(std::span<const std::byte> bytes) {
        if (bytes.size() < 8) return std::unexpected(error::invalid_format);

        uint64_t phf_sz = 0;
        std::memcpy(&phf_sz, bytes.data(), 8);
        if (8 + phf_sz > bytes.size()) return std::unexpected(error::invalid_format);

        auto phf_span = bytes.subspan(8, static_cast<size_t>(phf_sz));
        auto phf_r = PHF::deserialize(phf_span);
        if (!phf_r) return std::unexpected(phf_r.error());

        size_t offset = 0;
        auto val_opt = packed_type::deserialize(bytes.subspan(8 + phf_sz), offset);
        if (!val_opt) return std::unexpected(error::invalid_format);

        phf_value_array out{};
        out.phf_ = std::move(*phf_r);
        out.values_ = std::move(*val_opt);
        return out;
    }

private:
    PHF phf_{};
    packed_type values_{};
};

} // namespace maph
