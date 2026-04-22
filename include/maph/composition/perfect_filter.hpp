/**
 * @file perfect_filter.hpp
 * @brief Composes a perfect hash function with fingerprint verification
 *
 * A perfect_filter pairs a PHF (keys -> slots) with a packed fingerprint
 * array (approximate membership). The result is an approximate filter
 * that can also return unique slot indices for accepted keys.
 *
 * @tparam PHF A type satisfying perfect_hash_function
 * @tparam FPBits Fingerprint width in bits (8, 16, or 32)
 */

#pragma once

#include "../core.hpp"
#include "../concepts/perfect_hash_function.hpp"
#include "../filters/packed_fingerprint.hpp"
#include <array>
#include <bit>
#include <cstring>
#include <span>
#include <vector>
#include <string>
#include <string_view>
#include <optional>

namespace maph {

template<perfect_hash_function PHF, unsigned FPBits = 16>
class perfect_filter {
    PHF phf_;
    packed_fingerprint_array<FPBits> fps_;

public:
    perfect_filter() = default;
    perfect_filter(perfect_filter&&) = default;
    perfect_filter& operator=(perfect_filter&&) = default;

    static perfect_filter build(PHF phf, const std::vector<std::string>& keys) {
        perfect_filter pf;
        pf.phf_ = std::move(phf);

        auto slot_fn = [&pf](std::string_view k) -> std::optional<size_t> {
            return std::optional<size_t>{pf.phf_.slot_for(k).value};
        };

        pf.fps_.build(keys, slot_fn, pf.phf_.range_size());
        return pf;
    }

    [[nodiscard]] bool contains(std::string_view key) const noexcept {
        auto slot = phf_.slot_for(key);
        return fps_.verify(key, slot.value);
    }

    [[nodiscard]] std::optional<slot_index> slot_for(std::string_view key) const noexcept {
        auto slot = phf_.slot_for(key);
        if (fps_.verify(key, slot.value)) return slot;
        return std::nullopt;
    }

    [[nodiscard]] const PHF& phf() const noexcept { return phf_; }

    [[nodiscard]] size_t num_keys() const noexcept { return phf_.num_keys(); }
    [[nodiscard]] size_t range_size() const noexcept { return phf_.range_size(); }

    [[nodiscard]] std::vector<std::byte> serialize() const {
        auto phf_bytes = phf_.serialize();
        auto fps_bytes = fps_.serialize();

        std::vector<std::byte> out;
        uint64_t phf_size = phf_bytes.size();
        auto size_bytes = std::bit_cast<std::array<std::byte, 8>>(phf_size);
        out.insert(out.end(), size_bytes.begin(), size_bytes.end());
        out.insert(out.end(), phf_bytes.begin(), phf_bytes.end());
        out.insert(out.end(), fps_bytes.begin(), fps_bytes.end());
        return out;
    }

    [[nodiscard]] static result<perfect_filter> deserialize(std::span<const std::byte> data) {
        if (data.size() < 8) return std::unexpected(error::invalid_format);

        uint64_t phf_size{};
        std::memcpy(&phf_size, data.data(), 8);

        if (8 + phf_size > data.size()) return std::unexpected(error::invalid_format);

        auto phf_span = data.subspan(8, phf_size);
        auto fps_span = data.subspan(8 + phf_size);

        auto phf = PHF::deserialize(phf_span);
        if (!phf) return std::unexpected(phf.error());

        auto fps = packed_fingerprint_array<FPBits>::deserialize(fps_span);
        if (!fps) return std::unexpected(error::invalid_format);

        perfect_filter pf;
        pf.phf_ = std::move(*phf);
        pf.fps_ = std::move(*fps);
        return pf;
    }
};

} // namespace maph
