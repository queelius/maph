/**
 * @file serialization.hpp
 * @brief Shared serialization helpers for PHF algorithms.
 *
 * Small append/read primitives used by every PHF's serialize/deserialize
 * pair. Counts are encoded as uint64_t for 32/64-bit portability and
 * element counts are bounded to prevent OOM from crafted input.
 *
 * Requires little-endian byte order. All modern x86/ARM targets are LE.
 */

#pragma once

#include "../core.hpp"
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

namespace maph {

// Magic numbers for serialization format
constexpr uint32_t PERFECT_HASH_MAGIC = 0x4D415048;  // "MAPH"
constexpr uint32_t PERFECT_HASH_VERSION = 2;

// Serialization requires little-endian. All modern x86/ARM targets are LE.
// If you need big-endian support, add byte-swap wrappers around read/write.
static_assert(std::endian::native == std::endian::little,
    "maph serialization assumes little-endian byte order");

// Maximum element count allowed in a deserialized vector to prevent OOM
// from crafted input. 2^40 elements (~1 trillion) is generous but bounded.
constexpr uint64_t MAX_SERIALIZED_ELEMENT_COUNT = 1ULL << 40;

namespace phf_serial {

/// Append a trivially-copyable value to the byte buffer.
template<typename T>
inline void append(std::vector<std::byte>& buf, const T& value) {
    auto bytes = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
    buf.insert(buf.end(), bytes.begin(), bytes.end());
}

/// Append a vector with length prefix. Values of type U (or convertible)
/// are written with one append call per element; the count is a uint64_t.
template<typename Vec>
inline void append_vector(std::vector<std::byte>& buf, const Vec& v) {
    append(buf, static_cast<uint64_t>(v.size()));
    for (const auto& e : v) append(buf, e);
}

/// Append a size_t vector, widening each element to uint64_t for portability.
inline void append_vector_size(std::vector<std::byte>& buf,
                               const std::vector<size_t>& v) {
    append(buf, static_cast<uint64_t>(v.size()));
    for (auto e : v) append(buf, static_cast<uint64_t>(e));
}

/// Bounded stream reader over a byte span. Tracks offset, returns false
/// on any short read; never throws.
class reader {
    std::span<const std::byte> data_;
    size_t off_{0};
public:
    explicit reader(std::span<const std::byte> d) noexcept : data_(d) {}

    size_t remaining() const noexcept { return data_.size() - off_; }

    template<typename T>
    [[nodiscard]] bool read(T& out) noexcept {
        if (off_ + sizeof(T) > data_.size()) return false;
        std::memcpy(&out, data_.data() + off_, sizeof(T));
        off_ += sizeof(T);
        return true;
    }

    /// Read a length-prefixed vector of fixed-size T elements.
    template<typename T>
    [[nodiscard]] bool read_vector(std::vector<T>& out) noexcept {
        uint64_t count{};
        if (!read(count) || count > MAX_SERIALIZED_ELEMENT_COUNT) return false;
        auto n = static_cast<size_t>(count);
        if (n > remaining() / sizeof(T)) return false;
        out.resize(n);
        for (auto& e : out) { if (!read(e)) return false; }
        return true;
    }

    /// Read a length-prefixed vector of uint64_t and narrow each to size_t.
    [[nodiscard]] bool read_vector_size(std::vector<size_t>& out) noexcept {
        uint64_t count{};
        if (!read(count) || count > MAX_SERIALIZED_ELEMENT_COUNT) return false;
        auto n = static_cast<size_t>(count);
        if (n > remaining() / sizeof(uint64_t)) return false;
        out.resize(n);
        for (auto& e : out) {
            uint64_t v{};
            if (!read(v)) return false;
            e = static_cast<size_t>(v);
        }
        return true;
    }

    /// Bind `out` to the next `size` bytes of the underlying buffer and
    /// advance the cursor. Returns false without touching the cursor if the
    /// request would read past the end.
    [[nodiscard]] bool read_span(std::span<const std::byte>& out, size_t size) noexcept {
        if (size > remaining()) return false;
        out = std::span<const std::byte>(data_.data() + off_, size);
        off_ += size;
        return true;
    }
};

/// Verify the standard header (magic + version + algorithm id). Optionally
/// reads an additional trailing uint32_t (e.g. LeafSize, NumLevels, AlphaInt)
/// that parameterizes the algorithm. Returns true on match.
inline bool verify_header(reader& r, uint32_t expected_algo,
                          std::optional<uint32_t> expected_param = std::nullopt) {
    uint32_t magic{}, version{}, algo{};
    if (!r.read(magic) || magic != PERFECT_HASH_MAGIC) return false;
    if (!r.read(version) || version != PERFECT_HASH_VERSION) return false;
    if (!r.read(algo) || algo != expected_algo) return false;
    if (expected_param) {
        uint32_t param{};
        if (!r.read(param) || param != *expected_param) return false;
    }
    return true;
}

/// Write the standard header. The param (if provided) is written as uint32_t.
inline void write_header(std::vector<std::byte>& buf, uint32_t algo,
                         std::optional<uint32_t> param = std::nullopt) {
    append(buf, PERFECT_HASH_MAGIC);
    append(buf, PERFECT_HASH_VERSION);
    append(buf, algo);
    if (param) append(buf, *param);
}

}  // namespace phf_serial

} // namespace maph
