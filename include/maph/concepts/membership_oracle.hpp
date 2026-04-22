/**
 * @file membership_oracle.hpp
 * @brief Concept for approximate membership oracles.
 *
 * A membership_oracle answers "is this key in the set?" with a bounded
 * false positive rate. Examples: Bloom filter, xor filter, ribbon filter,
 * packed_fingerprint_array (when combined with a PHF).
 */

#pragma once

#include "../core.hpp"
#include <string_view>

namespace maph {

/**
 * @concept membership_oracle
 * @brief A type that can answer approximate membership queries.
 *
 * For keys in the build set, `verify(key)` returns true.
 * For keys not in the build set, `verify(key)` returns true with
 * probability at most the oracle's false positive rate.
 */
template<typename O>
concept membership_oracle = requires(const O o, std::string_view key) {
    { o.verify(key) }        -> std::convertible_to<bool>;
    { o.bits_per_key() }     -> std::convertible_to<double>;
    { o.memory_bytes() }     -> std::convertible_to<size_t>;
};

} // namespace maph
