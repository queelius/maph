/**
 * @file fingerprint_hash.hpp
 * @brief Independent hash used for approximate membership fingerprints.
 *
 * SplitMix64 finalization applied to FNV-1a. Must be independent of
 * the internal mixing used by any perfect hash function so that a
 * spurious PHF collision does not imply a spurious fingerprint match.
 */

#pragma once

#include <cstdint>
#include <string_view>

namespace maph {

/// Independent hash for fingerprinting. Must differ from perfect hash internals.
/// SplitMix64 finalization applied to FNV-1a.
[[nodiscard]] inline uint64_t membership_fingerprint(std::string_view key) noexcept {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (unsigned char c : key) {
        h ^= c;
        h *= 0x100000001b3ULL;
    }
    h ^= h >> 30;
    h *= 0xbf58476d1ce4e5b9ULL;
    h ^= h >> 27;
    h *= 0x94d049bb133111ebULL;
    h ^= h >> 31;
    return h;
}

} // namespace maph
