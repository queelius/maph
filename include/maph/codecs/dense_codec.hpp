/**
 * @file dense_codec.hpp
 * @brief Bijective codec: |V| = 2^M, one pattern per logical value.
 *
 * The "no free codespace" codec. Each M-bit pattern decodes to exactly
 * one logical value; non-members of the build set get a uniformly
 * random value from V. Appropriate when you genuinely need 2^M distinct
 * values and have no bias preference for non-member behavior.
 *
 * When |V| is not a power of two, or when you want a skewed non-member
 * distribution, use padded_codec instead.
 */

#pragma once

#include "../concepts/codec.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace maph {

template <typename V, unsigned M>
    requires (M >= 1 && M <= 20)  // 2^M fits comfortably; cap avoids silly sizes
class dense_codec {
public:
    using logical_value = V;
    static constexpr unsigned value_bits = M;
    static constexpr uint64_t alphabet_size = uint64_t{1} << M;

    dense_codec() = default;

    explicit dense_codec(std::vector<V> alphabet) : alphabet_(std::move(alphabet)) {
        if (alphabet_.size() != alphabet_size) {
            throw std::invalid_argument("dense_codec: alphabet size must equal 2^M");
        }
        build_reverse_map();
    }

    dense_codec(std::initializer_list<V> alphabet)
        : dense_codec(std::vector<V>{alphabet}) {}

    [[nodiscard]] uint64_t encode(const V& v) const {
        auto it = to_index_.find(v);
        if (it == to_index_.end()) {
            throw std::out_of_range("dense_codec::encode: value not in alphabet");
        }
        return it->second;
    }

    [[nodiscard]] V decode(uint64_t pattern) const {
        return alphabet_[pattern & (alphabet_size - 1)];
    }

    [[nodiscard]] const std::vector<V>& alphabet() const noexcept { return alphabet_; }

    // Expected probability that a non-member decodes to `v`.
    // Uniform for dense_codec: 1/2^M.
    [[nodiscard]] static constexpr double nonmember_probability(const V& /*v*/) {
        return 1.0 / static_cast<double>(alphabet_size);
    }

private:
    std::vector<V> alphabet_{};
    std::unordered_map<V, uint64_t> to_index_{};

    void build_reverse_map() {
        to_index_.reserve(alphabet_.size());
        for (uint64_t i = 0; i < alphabet_.size(); ++i) {
            to_index_.emplace(alphabet_[i], i);
        }
    }
};

} // namespace maph
