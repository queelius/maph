/**
 * @file packed_value_array.hpp
 * @brief Tightly packed array of M-bit values over 64-bit words.
 *
 * General-purpose bit-packed storage used by retrieval and filter
 * structures. Not tied to keys, hashes, or membership semantics; just
 * indexed M-bit reads and writes.
 *
 * M is a compile-time parameter in [1, 64]. At M=1 this is a bit
 * vector; at M=8/16/32/64 the per-value reads reduce to aligned or
 * nearly-aligned word loads. Intermediate widths pay a conditional
 * second-word load for values that cross a 64-bit boundary.
 */

#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

namespace maph::detail {

template <unsigned M>
    requires (M >= 1 && M <= 64)
class packed_value_array {
    static constexpr uint64_t value_mask_ =
        (M == 64) ? ~uint64_t{0} : ((uint64_t{1} << M) - 1);

    std::vector<uint64_t> data_{};
    size_t num_slots_{0};

public:
    static constexpr unsigned bits_per_value = M;

    // Smallest unsigned integer type that fits M bits.
    using value_type =
        std::conditional_t<(M <= 8),  uint8_t,
        std::conditional_t<(M <= 16), uint16_t,
        std::conditional_t<(M <= 32), uint32_t, uint64_t>>>;

    packed_value_array() = default;

    void resize(size_t num_slots) {
        num_slots_ = num_slots;
        size_t total_bits = num_slots * M;
        data_.assign((total_bits + 63) / 64, 0);
    }

    [[nodiscard]] size_t num_slots() const noexcept { return num_slots_; }

    [[nodiscard]] value_type get(size_t slot) const noexcept {
        size_t bit_pos = slot * M;
        size_t word_idx = bit_pos / 64;
        size_t bit_offset = bit_pos % 64;
        uint64_t val = data_[word_idx] >> bit_offset;
        if (bit_offset + M > 64 && word_idx + 1 < data_.size()) {
            val |= data_[word_idx + 1] << (64 - bit_offset);
        }
        return static_cast<value_type>(val & value_mask_);
    }

    void set(size_t slot, value_type value) noexcept {
        uint64_t v = static_cast<uint64_t>(value) & value_mask_;
        size_t bit_pos = slot * M;
        size_t word_idx = bit_pos / 64;
        size_t bit_offset = bit_pos % 64;

        data_[word_idx] &= ~(value_mask_ << bit_offset);
        data_[word_idx] |= v << bit_offset;

        if (bit_offset + M > 64 && word_idx + 1 < data_.size()) {
            size_t bits_in_first = 64 - bit_offset;
            size_t bits_in_second = M - bits_in_first;
            uint64_t mask2 = (uint64_t{1} << bits_in_second) - 1;
            data_[word_idx + 1] &= ~mask2;
            data_[word_idx + 1] |= (v >> bits_in_first) & mask2;
        }
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
        append(static_cast<uint32_t>(M));
        append(static_cast<uint64_t>(num_slots_));
        append(static_cast<uint64_t>(data_.size()));
        for (auto w : data_) append(w);
        return out;
    }

    // Deserialize starting at offset (updated in place).
    [[nodiscard]] static std::optional<packed_value_array>
    deserialize(std::span<const std::byte> bytes, size_t& offset) {
        auto read = [&](auto& val) -> bool {
            if (offset + sizeof(val) > bytes.size()) return false;
            std::memcpy(&val, bytes.data() + offset, sizeof(val));
            offset += sizeof(val);
            return true;
        };
        uint32_t width{}; uint64_t slots{}; uint64_t words{};
        if (!read(width) || width != M) return std::nullopt;
        if (!read(slots) || !read(words)) return std::nullopt;
        if (words > (bytes.size() - offset) / sizeof(uint64_t)) return std::nullopt;

        packed_value_array r;
        r.num_slots_ = static_cast<size_t>(slots);
        r.data_.resize(static_cast<size_t>(words));
        for (auto& w : r.data_) {
            if (!read(w)) return std::nullopt;
        }
        return r;
    }
};

} // namespace maph::detail
