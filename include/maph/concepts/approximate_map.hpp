/**
 * @file approximate_map.hpp
 * @brief Concept for approximate maps: keys to unique slots with bounded FPR.
 *
 * An approximate_map composes a perfect hash function with a membership
 * oracle. For keys in the build set, slot_for(key) returns the unique
 * slot assigned by the PHF. For keys not in the set, slot_for() returns
 * nullopt (with a bounded false positive rate for returning a spurious slot).
 *
 * perfect_filter<PHF, FPBits> is one concrete instance.
 */

#pragma once

#include "../core.hpp"
#include <optional>
#include <string_view>

namespace maph {

template<typename M>
concept approximate_map = requires(const M m, std::string_view key) {
    { m.contains(key) }      -> std::convertible_to<bool>;
    { m.slot_for(key) }      -> std::convertible_to<std::optional<slot_index>>;
    { m.num_keys() }         -> std::convertible_to<size_t>;
    { m.range_size() }       -> std::convertible_to<size_t>;
};

} // namespace maph
