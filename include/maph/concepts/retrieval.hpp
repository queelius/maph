/**
 * @file retrieval.hpp
 * @brief Concept for static functions (retrieval data structures).
 *
 * A retrieval data structure stores a value function v: S -> {0,1}^m
 * and supports lookup(k) = v(k) for k in S. For k not in S, lookup(k)
 * returns some value deterministic in the structure's contents — no
 * exception, no sentinel, no branch. This is the "garbage in, garbage
 * out" property that distinguishes retrieval from approximate_map.
 *
 * Formally: a retrieval structure is allowed to be a pure function of
 * key and stored state, with no membership-dependent control flow.
 *
 * Implications:
 *   - For k in S: lookup(k) = v(k) exactly.
 *   - For k not in S: lookup(k) is arbitrary. When the value range is
 *     pseudorandom (e.g., ciphertexts), the output distribution for
 *     non-members is statistically indistinguishable from a valid
 *     lookup. No membership oracle is leaked through the query path.
 *
 * This makes retrieval the correct primitive for oblivious
 * constructions such as cipher maps, where the *absence* of a
 * membership sentinel is a feature rather than a limitation.
 *
 * Space lower bound: value_bits() bits per key (Shannon; one needs at
 * least value_bits bits per key to recover value_bits bits of
 * information). Ribbon retrieval approaches this bound at ~1.04 M
 * bits/key. The naive PHF + array approach uses (PHF_bits + M)
 * bits/key, wasting the PHF overhead.
 *
 * Relationship to other concepts:
 *   - perfect_hash_function: a PHF is a retrieval function whose
 *     value_type happens to be slot_index and whose values are unique
 *     on S. Every PHF satisfies retrieval.
 *   - approximate_map: a retrieval paired with a membership oracle
 *     that gates the lookup behind a verify() check.
 *   - membership_oracle: orthogonal; can be composed with a retrieval
 *     to produce an approximate_map.
 */

#pragma once

#include <concepts>
#include <cstddef>
#include <string_view>
#include <vector>

namespace maph {

/**
 * @concept retrieval
 * @brief A static function mapping keys in S to m-bit values, garbage on non-S.
 *
 * Required interface:
 *   - value_type       nested type of the returned value
 *   - lookup(key)      retrieval (returns value_type, unconditionally)
 *   - num_keys()       size of the build set S
 *   - value_bits()     width m of each stored value
 *   - bits_per_key()   total space cost per key, including metadata
 *   - memory_bytes()   in-memory footprint
 *   - serialize()      byte-vector serialization
 */
template <typename T>
concept retrieval = requires(const T t, std::string_view key) {
    typename T::value_type;
    { t.lookup(key) }     -> std::convertible_to<typename T::value_type>;
    { t.num_keys() }      -> std::convertible_to<size_t>;
    { t.value_bits() }    -> std::convertible_to<size_t>;
    { t.bits_per_key() }  -> std::convertible_to<double>;
    { t.memory_bytes() }  -> std::convertible_to<size_t>;
    { t.serialize() }     -> std::convertible_to<std::vector<std::byte>>;
};

} // namespace maph
