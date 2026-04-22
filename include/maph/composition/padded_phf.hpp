/**
 * @file padded_phf.hpp
 * @brief Inflate a minimal PHF into a non-minimal PHF with controlled padding.
 *
 * Given a minimal PHF (slot_for returns values in [0, num_keys)), wrap it
 * into a PHF whose range is num_keys * padding_factor. Each key's padded
 * slot is inner_slot * padding_factor + offset(key), where offset is
 * derived from an independent hash of the key. This preserves the
 * perfect-hash property for the original key set (distinct inner slots
 * land in distinct padded rows) while leaving padding_factor - 1 out of
 * every padding_factor padded slots unused.
 *
 * Why bother: the unused slots are the mechanism by which pva-based
 * approximate maps can give non-members a controlled default value.
 * Combined with padded_codec's default_pattern and
 * phf_value_array::builder::with_fill_pattern, unused slots carry the
 * codec's default, so non-members whose query lands there decode to
 * the intended default value.
 *
 * Concrete shape. With a 100K-key MPHF and padding_factor=9, the
 * padded range is 900K. An encoded_retrieval built over the padded
 * structure will have about 100K used slots and 800K default-fill
 * slots. Non-members are roughly uniform over the padded range, so
 * about 89% land on default-fill slots and decode to the codec's
 * default. The remaining 11% land on used slots and return whatever
 * value some S-member stored there.
 *
 * Cost. The values array grows by padding_factor (one byte per padded
 * slot). The inner PHF's size is unchanged. For M-bit values and
 * padding factor k, space per S-key is inner.bits_per_key + M * k.
 * Whether this is worthwhile depends on how much of X can be "ignored"
 * and how much you care about the default-on-non-member property.
 */

#pragma once

#include "../concepts/perfect_hash_function.hpp"
#include "../core.hpp"
#include "../detail/hash.hpp"
#include "../detail/serialization.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace maph {

template <perfect_hash_function Inner>
class padded_phf {
public:
    padded_phf() = default;

    padded_phf(Inner inner, uint64_t padding_factor, uint64_t pad_seed)
        : inner_(std::move(inner)),
          padding_factor_(padding_factor),
          pad_seed_(pad_seed) {}

    [[nodiscard]] slot_index slot_for(std::string_view key) const noexcept {
        uint64_t m = static_cast<uint64_t>(inner_.slot_for(key).value);
        uint64_t off = offset_for(key);
        return slot_index{m * padding_factor_ + off};
    }

    [[nodiscard]] size_t num_keys() const noexcept { return inner_.num_keys(); }

    [[nodiscard]] size_t range_size() const noexcept {
        return static_cast<size_t>(inner_.range_size()) *
               static_cast<size_t>(padding_factor_);
    }

    [[nodiscard]] double bits_per_key() const noexcept {
        // The wrapper itself adds only a fixed seed + factor. The
        // dominant space cost is the inner PHF plus any padding-aware
        // values array (accounted for by the enclosing retrieval).
        return inner_.bits_per_key();
    }

    [[nodiscard]] size_t memory_bytes() const noexcept {
        return inner_.memory_bytes() + sizeof(padding_factor_) + sizeof(pad_seed_);
    }

    [[nodiscard]] uint64_t padding_factor() const noexcept { return padding_factor_; }
    [[nodiscard]] const Inner& inner() const noexcept { return inner_; }

    [[nodiscard]] std::vector<std::byte> serialize() const {
        std::vector<std::byte> out;
        auto append = [&](const auto& v) {
            auto b = std::bit_cast<std::array<std::byte, sizeof(v)>>(v);
            out.insert(out.end(), b.begin(), b.end());
        };
        append(padding_factor_);
        append(pad_seed_);
        auto inner_bytes = inner_.serialize();
        append(static_cast<uint64_t>(inner_bytes.size()));
        out.insert(out.end(), inner_bytes.begin(), inner_bytes.end());
        return out;
    }

    [[nodiscard]] static result<padded_phf>
    deserialize(std::span<const std::byte> bytes) {
        size_t off = 0;
        auto read = [&](auto& v) -> bool {
            if (off + sizeof(v) > bytes.size()) return false;
            std::memcpy(&v, bytes.data() + off, sizeof(v));
            off += sizeof(v);
            return true;
        };
        uint64_t factor{}, seed{}, inner_len{};
        if (!read(factor) || !read(seed) || !read(inner_len)) {
            return std::unexpected(error::invalid_format);
        }
        if (off + inner_len > bytes.size()) {
            return std::unexpected(error::invalid_format);
        }
        auto inner_r = Inner::deserialize(bytes.subspan(off, static_cast<size_t>(inner_len)));
        if (!inner_r) return std::unexpected(inner_r.error());
        return padded_phf{std::move(*inner_r), factor, seed};
    }

    // ===== Builder =====

    class builder {
        typename Inner::builder inner_builder_{};
        uint64_t padding_factor_{2};
        uint64_t pad_seed_{0xa076'1d64'78bd'642fULL};

    public:
        builder() = default;

        builder& add(std::string_view key) {
            inner_builder_.add(key);
            return *this;
        }

        builder& add_all(std::span<const std::string> keys) {
            inner_builder_.add_all(keys);
            return *this;
        }

        builder& add_all(const std::vector<std::string>& keys) {
            inner_builder_.add_all(keys);
            return *this;
        }

        // Stride: padded range = inner.range * padding_factor.
        // padding_factor = 1 is a no-op wrapper.
        builder& with_padding(uint64_t factor) {
            padding_factor_ = factor < 1 ? 1 : factor;
            return *this;
        }

        // Seed for the offset-within-row hash. Independent of inner seeds.
        builder& with_pad_seed(uint64_t s) { pad_seed_ = s; return *this; }

        // Forward inner-PHF builder knobs when present.
        builder& with_seed(uint64_t seed)
            requires requires(typename Inner::builder& b) { b.with_seed(seed); } {
            inner_builder_.with_seed(seed);
            return *this;
        }

        builder& with_threads(size_t n)
            requires requires(typename Inner::builder& b) { b.with_threads(n); } {
            inner_builder_.with_threads(n);
            return *this;
        }

        [[nodiscard]] result<padded_phf> build() {
            auto r = inner_builder_.build();
            if (!r) return std::unexpected(r.error());
            return padded_phf{std::move(*r), padding_factor_, pad_seed_};
        }
    };

private:
    Inner inner_{};
    uint64_t padding_factor_{1};
    uint64_t pad_seed_{0};

    // Offset within the row for a given key. Uses an independent hash
    // of the key keyed by pad_seed_ so the choice is decorrelated from
    // the inner PHF's slot assignment.
    uint64_t offset_for(std::string_view key) const noexcept {
        uint64_t h = phf_hash_with_seed(key, pad_seed_);
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccdULL;
        h ^= h >> 33;
        return h % padding_factor_;
    }
};

} // namespace maph
