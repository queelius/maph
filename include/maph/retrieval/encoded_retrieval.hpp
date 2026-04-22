/**
 * @file encoded_retrieval.hpp
 * @brief Retrieval over a logical value alphabet via a codec.
 *
 * Composes any retrieval implementation with any codec: the retrieval
 * stores M-bit patterns; the codec maps between those patterns and the
 * user's logical values. Satisfies the retrieval concept with
 * value_type = codec::logical_value.
 *
 * Non-member behavior depends on the base retrieval:
 *
 *   - MPHF-based pva (phobic, bbhash, recsplit, etc., all minimal):
 *     every slot is used, so a non-member query returns some in-S
 *     member's stored pattern. Non-member value distribution matches
 *     the in-S value distribution. Codec controls logical decoding,
 *     not the shape of the distribution.
 *
 *   - Non-minimal pva (range > num_keys): unused slots can be filled
 *     via pva::builder::with_fill_pattern. encoded_retrieval wires
 *     codec.default_pattern() there automatically, so non-members
 *     landing in unused slots decode to the codec's default value.
 *     The mix is (in-S value distribution) on used slots and
 *     (default value) on unused slots.
 *
 *   - Ribbon retrieval: non-member lookups return XOR of stored
 *     solution entries. The distribution is controlled by the
 *     linear span of stored patterns in GF(2)^M. With all stored
 *     values identical, the span collapses to one pattern; with
 *     diverse stored values, the span fills more of the codespace.
 *
 * No membership check is performed. This is the "garbage in, garbage
 * out" semantics of the retrieval primitive: garbage is drawn from
 * the logical value alphabet rather than raw bits.
 *
 * For oblivious constructions where non-member output must follow a
 * specific distribution (e.g., dominant value for cipher maps), the
 * cleanest approach is to ensure the in-S value distribution itself
 * matches the target, or to use a non-minimal pva with a suitable
 * fill pattern.
 */

#pragma once

#include "../concepts/codec.hpp"
#include "../concepts/retrieval.hpp"
#include "../core.hpp"

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

template <typename Retrieval, typename Codec>
    requires codec<Codec>
         && retrieval<Retrieval>
         && (Codec::value_bits == Retrieval::value_bits_v)
class encoded_retrieval {
public:
    using base_type = Retrieval;
    using codec_type = Codec;
    using value_type = typename Codec::logical_value;
    using pattern_type = typename Retrieval::value_type;

    static constexpr unsigned value_bits_v = Retrieval::value_bits_v;

    encoded_retrieval() = default;

    encoded_retrieval(Retrieval base, Codec c)
        : base_(std::move(base)), codec_(std::move(c)) {}

    [[nodiscard]] value_type lookup(std::string_view key) const {
        return codec_.decode(static_cast<uint64_t>(base_.lookup(key)));
    }

    [[nodiscard]] size_t num_keys() const noexcept { return base_.num_keys(); }
    [[nodiscard]] size_t value_bits() const noexcept { return base_.value_bits(); }
    [[nodiscard]] double bits_per_key() const noexcept { return base_.bits_per_key(); }
    [[nodiscard]] size_t memory_bytes() const noexcept { return base_.memory_bytes(); }

    [[nodiscard]] const Retrieval& base() const noexcept { return base_; }
    [[nodiscard]] const Codec& encoder() const noexcept { return codec_; }

    // Serialize only the base retrieval. Codecs with runtime state
    // (alphabets, maps) are the caller's responsibility to reconstruct
    // and pass back to deserialize.
    [[nodiscard]] std::vector<std::byte> serialize() const {
        return base_.serialize();
    }

    [[nodiscard]] static result<encoded_retrieval>
    deserialize(std::span<const std::byte> bytes, Codec c) {
        auto r = Retrieval::deserialize(bytes);
        if (!r) return std::unexpected(r.error());
        return encoded_retrieval{std::move(*r), std::move(c)};
    }

    // ===== Builder =====
    //
    // Holds a codec and an inner retrieval builder. add(key, logical_value)
    // encodes the value to a pattern and forwards to the inner builder.

    class builder {
        typename Retrieval::builder rbuilder_{};
        Codec codec_{};

    public:
        builder() = default;
        explicit builder(Codec c) : codec_(std::move(c)) {}

        builder& add(std::string_view key, const value_type& v) {
            rbuilder_.add(key, static_cast<pattern_type>(codec_.encode(v)));
            return *this;
        }

        builder& add_all(std::span<const std::string> keys,
                         std::span<const value_type> values) {
            size_t n = keys.size() < values.size() ? keys.size() : values.size();
            for (size_t i = 0; i < n; ++i) {
                rbuilder_.add(keys[i],
                    static_cast<pattern_type>(codec_.encode(values[i])));
            }
            return *this;
        }

        template <typename ValueFn>
            requires std::invocable<ValueFn, std::string_view>
        builder& add_all_with(std::span<const std::string> keys, ValueFn fn) {
            for (const auto& k : keys) {
                value_type v = fn(std::string_view{k});
                rbuilder_.add(k, static_cast<pattern_type>(codec_.encode(v)));
            }
            return *this;
        }

        // Forward inner-retrieval knobs when present.
        builder& with_seed(uint64_t seed)
            requires requires(typename Retrieval::builder& b) { b.with_seed(seed); } {
            rbuilder_.with_seed(seed);
            return *this;
        }

        builder& with_threads(size_t n)
            requires requires(typename Retrieval::builder& b) { b.with_threads(n); } {
            rbuilder_.with_threads(n);
            return *this;
        }

        [[nodiscard]] result<encoded_retrieval> build() {
            // If the codec exposes a default pattern and the base
            // retrieval builder supports a fill pattern for unused
            // slots, wire them together. This gives "unused slot
            // decodes to default" semantics for pva with non-minimal
            // PHFs, so non-members whose query lands in an unused slot
            // produce the codec's intended default value.
            if constexpr (
                requires(Codec& c) { c.default_pattern(); } &&
                requires(typename Retrieval::builder& b, pattern_type p) {
                    b.with_fill_pattern(p);
                })
            {
                rbuilder_.with_fill_pattern(
                    static_cast<pattern_type>(codec_.default_pattern()));
            }
            auto r = rbuilder_.build();
            if (!r) return std::unexpected(r.error());
            return encoded_retrieval{std::move(*r), std::move(codec_)};
        }
    };

private:
    Retrieval base_{};
    Codec codec_{};
};

} // namespace maph
