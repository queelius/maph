/**
 * @file packed_fingerprint.hpp
 * @brief Packed fingerprint array for compact membership verification.
 *
 * Stores a k-bit fingerprint per slot in a tightly packed bit array.
 * Combined with a perfect hash function in composition/perfect_filter.hpp
 * to form an approximate map.
 */

#pragma once

#include "../core.hpp"
#include "../detail/fingerprint_hash.hpp"
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace maph {

/**
 * @class packed_fingerprint_array
 * @brief Stores k-bit fingerprints in a tightly packed bit array
 *
 * Space: exactly FingerprintBits bits per key.
 * Query: extract k-bit fingerprint from packed array, compare.
 * FP rate: 2^-FingerprintBits.
 *
 * @tparam FingerprintBits Width of each fingerprint (1-32)
 */
template<unsigned FingerprintBits>
    requires (FingerprintBits >= 1 && FingerprintBits <= 32)
class packed_fingerprint_array {
    static constexpr uint64_t fp_mask = (1ULL << FingerprintBits) - 1;

    std::vector<uint64_t> data_;
    size_t num_slots_{0};

    static uint64_t truncate_fp(std::string_view key) noexcept {
        return membership_fingerprint(key) & fp_mask;
    }

    uint64_t extract(size_t slot) const noexcept {
        size_t bit_pos = slot * FingerprintBits;
        size_t word_idx = bit_pos / 64;
        size_t bit_offset = bit_pos % 64;

        uint64_t val = data_[word_idx] >> bit_offset;
        if (bit_offset + FingerprintBits > 64 && word_idx + 1 < data_.size()) {
            val |= data_[word_idx + 1] << (64 - bit_offset);
        }
        return val & fp_mask;
    }

    void store(size_t slot, uint64_t value) noexcept {
        size_t bit_pos = slot * FingerprintBits;
        size_t word_idx = bit_pos / 64;
        size_t bit_offset = bit_pos % 64;

        data_[word_idx] &= ~(fp_mask << bit_offset);
        data_[word_idx] |= (value & fp_mask) << bit_offset;

        if (bit_offset + FingerprintBits > 64 && word_idx + 1 < data_.size()) {
            size_t bits_in_first = 64 - bit_offset;
            size_t bits_in_second = FingerprintBits - bits_in_first;
            uint64_t mask2 = (1ULL << bits_in_second) - 1;
            data_[word_idx + 1] &= ~mask2;
            data_[word_idx + 1] |= (value >> bits_in_first) & mask2;
        }
    }

public:
    packed_fingerprint_array() = default;

    void build(const std::vector<std::string>& keys,
               std::function<std::optional<size_t>(std::string_view)> slot_for,
               size_t total_slots) {
        num_slots_ = total_slots;
        size_t total_bits = num_slots_ * FingerprintBits;
        data_.assign((total_bits + 63) / 64, 0);

        for (const auto& key : keys) {
            if (auto slot = slot_for(key)) {
                store(*slot, truncate_fp(key));
            }
        }
    }

    [[nodiscard]] bool verify(std::string_view key, size_t slot) const noexcept {
        if (slot >= num_slots_) return false;
        return extract(slot) == truncate_fp(key);
    }

    [[nodiscard]] double bits_per_key(size_t key_count) const noexcept {
        return key_count > 0 ? static_cast<double>(FingerprintBits) : 0.0;
    }

    [[nodiscard]] size_t memory_bytes() const noexcept {
        return data_.size() * sizeof(uint64_t);
    }

    [[nodiscard]] std::vector<std::byte> serialize() const {
        std::vector<std::byte> out;
        auto append = [&](const auto& val) {
            auto bytes = std::bit_cast<std::array<std::byte, sizeof(val)>>(val);
            out.insert(out.end(), bytes.begin(), bytes.end());
        };
        append(static_cast<uint32_t>(FingerprintBits));
        append(static_cast<uint64_t>(num_slots_));
        append(static_cast<uint64_t>(data_.size()));
        for (auto w : data_) append(w);
        return out;
    }

    [[nodiscard]] static std::optional<packed_fingerprint_array> deserialize(std::span<const std::byte> bytes) {
        size_t off = 0;
        auto read = [&](auto& val) -> bool {
            if (off + sizeof(val) > bytes.size()) return false;
            std::memcpy(&val, bytes.data() + off, sizeof(val));
            off += sizeof(val);
            return true;
        };
        uint32_t fp_bits{}; uint64_t slots{}; uint64_t words{};
        if (!read(fp_bits) || fp_bits != FingerprintBits) return std::nullopt;
        if (!read(slots) || !read(words)) return std::nullopt;
        if (words > (bytes.size() - off) / sizeof(uint64_t)) return std::nullopt;

        packed_fingerprint_array r;
        r.num_slots_ = static_cast<size_t>(slots);
        r.data_.resize(static_cast<size_t>(words));
        for (auto& w : r.data_) { if (!read(w)) return std::nullopt; }
        return r;
    }
};

} // namespace maph
