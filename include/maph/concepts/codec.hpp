/**
 * @file codec.hpp
 * @brief Concept for codecs: logical values <-> M-bit patterns.
 *
 * A codec mediates between a user's logical value alphabet V and the
 * fixed-width M-bit patterns stored by a retrieval. It is deliberately
 * many-to-one in the decode direction: multiple M-bit patterns may map
 * to the same logical value. That extra codespace is the lever for
 * controlling non-member behavior of the enclosing retrieval.
 *
 * If V has |V| distinct values and the codec allocates k_v patterns to
 * value v (with sum k_v = 2^M), then for any key k not in the build set
 * S, the enclosing retrieval returns a value v with probability
 * k_v / 2^M. The caller designs this distribution by picking a codec.
 *
 * Use cases:
 *   - Bijective (dense): |V| = 2^M, uniform non-member distribution.
 *   - Padded: |V| < 2^M, surplus patterns decode to a designated
 *     default value. Non-members lean toward default.
 *   - Prefix-free: per-value bit lengths matching expected frequencies.
 *     Common values get more codespace, rare values less.
 *
 * Codecs compose with retrieval via encoded_retrieval<Retrieval, Codec>.
 */

#pragma once

#include <concepts>
#include <cstdint>

namespace maph {

/**
 * @concept codec
 * @brief Encode a logical value to an M-bit pattern and decode back.
 *
 * Required members:
 *   - logical_value   user-facing value type
 *   - value_bits      static constexpr unsigned (must equal M of the
 *                     retrieval the codec is paired with)
 *   - encode(v)       logical_value -> pattern (any valid pattern for v)
 *   - decode(pattern) pattern -> logical_value (deterministic on
 *                     every 2^M-bit pattern, including patterns that
 *                     map to a default value)
 *
 * Invariant: for any `v` in the codec's alphabet, decode(encode(v)) == v.
 * Patterns outside the image of encode for any alphabet value are
 * decoded to a designated default (codec-specific).
 */
template <typename C>
concept codec = requires(const C c,
                         typename C::logical_value v,
                         uint64_t pattern) {
    typename C::logical_value;
    { C::value_bits } -> std::convertible_to<unsigned>;
    { c.encode(v) }     -> std::convertible_to<uint64_t>;
    { c.decode(pattern) } -> std::convertible_to<typename C::logical_value>;
};

} // namespace maph
