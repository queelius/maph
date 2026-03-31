/**
 * @file phf_concept.hpp
 * @brief Concepts for perfect hash functions and their builders
 *
 * Defines the interface that all perfect hash function implementations
 * must satisfy. Separates the pure hash function (keys -> slots) from
 * membership verification and value storage.
 */

#pragma once

#include "core.hpp"
#include <string_view>
#include <vector>
#include <span>
#include <cstddef>

namespace maph {

/**
 * @concept perfect_hash_function
 * @brief A perfect hash function mapping n keys to distinct slots in [0, m)
 *
 * For keys in the build set, slot_for() returns a unique slot in [0, range_size()).
 * For keys NOT in the build set, slot_for() returns an arbitrary but valid index
 * in [0, range_size()). No membership checking is performed.
 *
 * When range_size() == num_keys(), the PHF is minimal (MPHF).
 * When range_size() > num_keys(), some slots are unused.
 */
template<typename P>
concept perfect_hash_function = requires(const P p, std::string_view key) {
    { p.slot_for(key) }    -> std::convertible_to<slot_index>;
    { p.num_keys() }       -> std::convertible_to<size_t>;
    { p.range_size() }     -> std::convertible_to<size_t>;
    { p.bits_per_key() }   -> std::convertible_to<double>;
    { p.memory_bytes() }   -> std::convertible_to<size_t>;
    { p.serialize() }      -> std::convertible_to<std::vector<std::byte>>;
};

/**
 * @concept phf_builder
 * @brief A builder that constructs a perfect hash function from a key set
 *
 * Builders use a fluent interface: add keys, then build.
 * build() may fail (e.g., if construction parameters are unsuitable).
 */
template<typename B, typename PHF>
concept phf_builder = requires(B b, std::string_view key, const std::vector<std::string>& keys) {
    { b.add(key) }       -> std::same_as<B&>;
    { b.add_all(keys) }  -> std::same_as<B&>;
    { b.build() }        -> std::same_as<result<PHF>>;
};

} // namespace maph
