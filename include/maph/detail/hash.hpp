/**
 * @file hash.hpp
 * @brief Shared hash primitives used by PHF algorithms.
 *
 * The splitmix64 finalizer and FNV-1a-with-seed are used identically by
 * several algorithms. Factored out to avoid duplication.
 */

#pragma once

#include <cstdint>
#include <string_view>

namespace maph {

/// SplitMix64 final mixer — strong avalanche in a few cycles.
[[nodiscard]] inline constexpr uint64_t phf_remix(uint64_t z) noexcept {
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

/// FNV-1a over the key bytes starting from `seed`, finalized with splitmix64.
[[nodiscard]] inline uint64_t phf_hash_with_seed(std::string_view key, uint64_t seed) noexcept {
    uint64_t h = seed;
    for (unsigned char c : key) {
        h ^= c;
        h *= 0x100000001b3ULL;  // FNV prime
    }
    return phf_remix(h);
}

} // namespace maph
