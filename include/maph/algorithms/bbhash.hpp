/**
 * @file bbhash.hpp
 * @brief BBHash (Bit-Block Hash) minimal perfect hash function.
 *
 * BBHash is a space-efficient minimal perfect hash function designed
 * for large datasets. It supports parallel construction via bit-level
 * collision resolution across multiple levels.
 *
 * References:
 * - Limasset et al. "Fast and Scalable Minimal Perfect Hashing for Massive Key Sets" (2017)
 * - Space: ~2.0-3.0 bits per key (depends on gamma)
 * - Query time: O(1), typically 30-50ns
 * - Build time: O(n/p) with p threads
 */

#pragma once

#include "../core.hpp"
#include "../concepts/perfect_hash_function.hpp"
#include "../detail/hash.hpp"
#include "../detail/serialization.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace maph {

/**
 * @class bbhash_hasher
 * @brief BBHash minimal perfect hash with parallel construction
 *
 * Algorithm:
 * 1. Keys are hashed into levels using different hash functions
 * 2. Each level uses a bit array to mark assigned slots
 * 3. Collision detection via bit-level operations (fast and parallel)
 * 4. Space parameter gamma controls bits/key (higher = faster build)
 *
 * @tparam NumLevels Maximum number of collision resolution levels (default 3)
 */
template<size_t NumLevels = 3>
class bbhash_hasher {
    static_assert(NumLevels >= 1 && NumLevels <= 10, "NumLevels must be between 1 and 10");

public:
    class builder;

private:
    // Bit array for each level indicating which slots are occupied
    struct level {
        std::vector<uint64_t> bits;    // Bit array
        std::vector<size_t> rank_checkpoints;  // Rank checkpoints for O(1) rank queries
        size_t num_keys{0};            // Keys assigned to this level
        uint64_t seed{0};              // Hash seed for this level

        [[nodiscard]] bool get_bit(size_t idx) const noexcept {
            if (idx / 64 >= bits.size()) return false;
            return (bits[idx / 64] >> (idx % 64)) & 1;
        }

        void set_bit(size_t idx) noexcept {
            size_t word_idx = idx / 64;
            if (word_idx < bits.size()) {
                bits[word_idx] |= (1ULL << (idx % 64));
            }
        }

        // Build rank structure for O(1) rank queries
        void build_rank_structure() noexcept {
            rank_checkpoints.resize(bits.size());
            size_t cumulative_rank = 0;
            for (size_t i = 0; i < bits.size(); ++i) {
                rank_checkpoints[i] = cumulative_rank;
                cumulative_rank += std::popcount(bits[i]);
            }
        }

        // O(1) rank query: count set bits before position idx
        [[nodiscard]] size_t rank(size_t idx) const noexcept {
            if (idx / 64 >= bits.size()) return num_keys;

            size_t word_idx = idx / 64;
            size_t bit_offset = idx % 64;

            // Checkpoint gives us rank up to start of this word
            size_t result = rank_checkpoints[word_idx];

            // Add popcount of bits before idx within this word
            if (bit_offset > 0) {
                uint64_t mask = (1ULL << bit_offset) - 1;
                result += std::popcount(bits[word_idx] & mask);
            }

            return result;
        }
    };

    std::array<level, NumLevels> levels_;
    size_t key_count_{0};
    size_t total_slots_{0};  // gamma * key_count
    double gamma_{2.0};      // Space-time trade-off parameter
    uint64_t base_seed_{0};

    explicit bbhash_hasher(size_t key_count, double gamma, uint64_t base_seed)
        : key_count_(key_count)
        , total_slots_(static_cast<size_t>(std::ceil(key_count * gamma)))
        , gamma_(gamma)
        , base_seed_(base_seed) {

        // Initialize level seeds and pre-allocate bit arrays
        std::mt19937_64 rng(base_seed);
        size_t words_needed = (total_slots_ + 63) / 64;
        for (size_t i = 0; i < NumLevels; ++i) {
            levels_[i].seed = rng();
            levels_[i].bits.resize(words_needed, 0);
        }
    }

    // Hash with level-specific seed
    [[nodiscard]] uint64_t hash_at_level(std::string_view key, size_t level_idx) const noexcept {
        return phf_hash_with_seed(key, levels_[level_idx].seed) % total_slots_;
    }

public:
    bbhash_hasher() = default;
    bbhash_hasher(bbhash_hasher&&) = default;
    bbhash_hasher& operator=(bbhash_hasher&&) = default;

    /**
     * @brief Get slot index for a key (deterministic, no verification)
     * @param key Key to look up
     * @return Slot index in [0, num_keys())
     */
    [[nodiscard]] slot_index slot_for(std::string_view key) const noexcept {
        if (key_count_ == 0) return slot_index{0};

        size_t cumulative_offset = 0;
        for (size_t level_idx = 0; level_idx < NumLevels; ++level_idx) {
            uint64_t slot = hash_at_level(key, level_idx);

            if (levels_[level_idx].get_bit(slot)) {
                return slot_index{cumulative_offset + levels_[level_idx].rank(slot)};
            }
            cumulative_offset += levels_[level_idx].num_keys;
        }

        // Key not in build set, return arbitrary valid index
        return slot_index{0};
    }

    [[nodiscard]] size_t num_keys() const noexcept { return key_count_; }
    [[nodiscard]] size_t range_size() const noexcept { return key_count_; }

    [[nodiscard]] double bits_per_key() const noexcept {
        if (key_count_ == 0) return 0.0;
        // BBHash space usage is approximately gamma bits per key
        size_t active_levels = 0;
        for (const auto& level : levels_) {
            if (level.num_keys > 0) ++active_levels;
        }
        size_t total_bits = active_levels * total_slots_;
        size_t rank_bytes = 0;
        for (const auto& level : levels_) {
            rank_bytes += level.rank_checkpoints.size() * sizeof(size_t);
        }
        size_t total_bytes = (total_bits / 8) + rank_bytes + sizeof(*this);
        return (total_bytes * 8.0) / key_count_;
    }

    [[nodiscard]] size_t memory_bytes() const noexcept {
        size_t active_levels = 0;
        for (const auto& level : levels_) {
            if (level.num_keys > 0) ++active_levels;
        }
        size_t total_bits = active_levels * total_slots_;
        size_t rank_bytes = 0;
        for (const auto& level : levels_) {
            rank_bytes += level.rank_checkpoints.size() * sizeof(size_t);
        }
        return (total_bits / 8) + rank_bytes + sizeof(*this);
    }

    [[nodiscard]] double gamma() const noexcept { return gamma_; }

    // Algorithm identifier for serialization
    static constexpr uint32_t ALGORITHM_ID = 3;  // BBHash

    [[nodiscard]] std::vector<std::byte> serialize() const {
        std::vector<std::byte> out;
        phf_serial::write_header(out, ALGORITHM_ID, static_cast<uint32_t>(NumLevels));

        phf_serial::append(out, static_cast<uint64_t>(key_count_));
        phf_serial::append(out, static_cast<uint64_t>(total_slots_));
        phf_serial::append(out, gamma_);
        phf_serial::append(out, base_seed_);

        for (const auto& lvl : levels_) {
            phf_serial::append_vector(out, lvl.bits);
            phf_serial::append_vector_size(out, lvl.rank_checkpoints);
            phf_serial::append(out, static_cast<uint64_t>(lvl.num_keys));
            phf_serial::append(out, lvl.seed);
        }
        return out;
    }

    [[nodiscard]] static result<bbhash_hasher> deserialize(std::span<const std::byte> data) {
        phf_serial::reader r(data);

        if (!phf_serial::verify_header(r, ALGORITHM_ID, static_cast<uint32_t>(NumLevels))) {
            return std::unexpected(error::invalid_format);
        }

        uint64_t key_count_u64{}, total_slots_u64{}, base_seed{};
        double gamma{};
        if (!r.read(key_count_u64) || !r.read(total_slots_u64) ||
            !r.read(gamma) || !r.read(base_seed)) {
            return std::unexpected(error::invalid_format);
        }
        if (key_count_u64 > MAX_SERIALIZED_ELEMENT_COUNT) {
            return std::unexpected(error::invalid_format);
        }

        bbhash_hasher hasher(static_cast<size_t>(key_count_u64), gamma, base_seed);
        hasher.total_slots_ = static_cast<size_t>(total_slots_u64);

        for (auto& lvl : hasher.levels_) {
            uint64_t num_keys{};
            if (!r.read_vector(lvl.bits) || !r.read_vector_size(lvl.rank_checkpoints) ||
                !r.read(num_keys) || !r.read(lvl.seed)) {
                return std::unexpected(error::invalid_format);
            }
            lvl.num_keys = static_cast<size_t>(num_keys);
        }
        return hasher;
    }

    /**
     * @class builder
     * @brief Builder for BBHash perfect hash
     *
     * Note: BBHash level construction is inherently sequential (each level
     * depends on knowing which keys collided at the previous level).
     * Use RecSplit with_threads() for parallel construction.
     */
    class builder {
        std::vector<std::string> keys_;
        double gamma_{2.0};  // Default space parameter (2x space = faster)
        uint64_t seed_{0x123456789abcdef0ULL};

    public:
        builder() = default;

        builder& add(std::string_view key) {
            keys_.emplace_back(key);
            return *this;
        }

        builder& add_all(std::span<const std::string> keys) {
            keys_.insert(keys_.end(), keys.begin(), keys.end());
            return *this;
        }

        builder& add_all(const std::vector<std::string>& keys) {
            keys_.insert(keys_.end(), keys.begin(), keys.end());
            return *this;
        }

        builder& with_gamma(double g) {
            if (g < 1.0) g = 1.0;
            if (g > 10.0) g = 10.0;
            gamma_ = g;
            return *this;
        }

        builder& with_seed(uint64_t seed) {
            seed_ = seed;
            return *this;
        }

        [[nodiscard]] result<bbhash_hasher> build() {
            if (keys_.empty()) {
                return std::unexpected(error::optimization_failed);
            }

            // Remove duplicates
            std::sort(keys_.begin(), keys_.end());
            keys_.erase(std::unique(keys_.begin(), keys_.end()), keys_.end());

            double current_gamma = gamma_;

            for (int attempt = 0; attempt < 100; ++attempt) {
                uint64_t attempt_seed = seed_ ^ (attempt * 0x9e3779b97f4a7c15ULL);

                // Bump gamma every 5 attempts (more aggressive for large key sets)
                if (attempt > 0 && attempt % 5 == 0) {
                    current_gamma += 0.5;
                }

                bbhash_hasher hasher(keys_.size(), current_gamma, attempt_seed);

                // Build BBHash structure level by level
                std::vector<std::string> remaining_keys = keys_;

                for (size_t level_idx = 0; level_idx < NumLevels && !remaining_keys.empty(); ++level_idx) {
                    auto& current_level = hasher.levels_[level_idx];
                    std::vector<std::string> next_level_keys;

                    // Track collisions using a temporary collision map
                    std::vector<size_t> slot_counts(hasher.total_slots_, 0);

                    // Hash all remaining keys to this level
                    for (const auto& key : remaining_keys) {
                        uint64_t slot = hasher.hash_at_level(key, level_idx);
                        slot_counts[slot]++;
                    }

                    // Assign non-colliding keys to this level
                    for (const auto& key : remaining_keys) {
                        uint64_t slot = hasher.hash_at_level(key, level_idx);
                        if (slot_counts[slot] == 1) {
                            // No collision - assign to this level
                            current_level.set_bit(slot);
                            current_level.num_keys++;
                        } else {
                            // Collision - push to next level
                            next_level_keys.push_back(key);
                        }
                    }

                    remaining_keys = std::move(next_level_keys);
                }

                // All keys must be placed (no remaining)
                if (!remaining_keys.empty()) {
                    continue;  // Retry with different seed
                }

                // Build rank structures for O(1) queries
                for (auto& level : hasher.levels_) {
                    level.build_rank_structure();
                }

                return hasher;
            }

            return std::unexpected(error::optimization_failed);
        }
    };
};

// ===== CONVENIENCE ALIASES =====

using bbhash3 = bbhash_hasher<3>;
using bbhash5 = bbhash_hasher<5>;

// ===== FACTORY FUNCTIONS =====

/**
 * @brief Create a BBHash hasher from keys
 */
template<size_t NumLevels = 3>
[[nodiscard]] inline result<bbhash_hasher<NumLevels>>
make_bbhash(std::span<const std::string> keys, double gamma = 2.0, uint64_t seed = 0) {
    typename bbhash_hasher<NumLevels>::builder builder;
    return builder.add_all(keys).with_gamma(gamma).with_seed(seed).build();
}

// ===== STATIC ASSERTIONS =====

static_assert(perfect_hash_function<bbhash_hasher<3>>);

} // namespace maph
