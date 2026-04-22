/**
 * @file padded_codec.hpp
 * @brief Injection from V to M-bit patterns with the surplus absorbed
 *        into a designated default value.
 *
 * For a logical alphabet V of size k, assigns patterns 0 through k-1
 * to the listed values, then decodes all remaining 2^M - k patterns
 * to a designated default. For any key not in the build set S, the
 * enclosing retrieval returns the default value with probability
 * (2^M - k) / 2^M = 1 - k/2^M.
 *
 * This is the primary tool for "garbage in, default out" cipher map
 * construction: pick a default that non-members should decode to
 * (often the dominant or most benign value), size M so that the
 * non-member leakage through the other k values is tolerable, and
 * build the retrieval on just the subset you care about.
 *
 * Example. A 4-value alphabet {A, B, C, D} with M=6:
 *   2^6 = 64 patterns total
 *   k = 4 indexed, pattern 0=A, 1=B, 2=C, 3=D
 *   pattern 4..63 all decode to (say) C as the default
 *   non-member probability: A = B = D = 1/64; C = 61/64 ~ 95%
 */

#pragma once

#include "../concepts/codec.hpp"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace maph {

template <typename V, unsigned M>
    requires (M >= 1 && M <= 32)
class padded_codec {
public:
    using logical_value = V;
    static constexpr unsigned value_bits = M;
    static constexpr uint64_t alphabet_size = uint64_t{1} << M;

    padded_codec() = default;

    // `indexed` is the ordered list of non-default values. `default_value`
    // is what every surplus pattern decodes to. If default_value appears
    // in `indexed`, it gets its explicit code and also absorbs the surplus.
    padded_codec(std::vector<V> indexed, V default_value)
        : indexed_(std::move(indexed)), default_(std::move(default_value))
    {
        if (indexed_.size() > alphabet_size) {
            throw std::invalid_argument("padded_codec: more values than 2^M patterns");
        }
        build_reverse_map();
    }

    padded_codec(std::initializer_list<V> indexed, V default_value)
        : padded_codec(std::vector<V>{indexed}, std::move(default_value)) {}

    [[nodiscard]] uint64_t encode(const V& v) const {
        auto it = to_index_.find(v);
        if (it != to_index_.end()) return it->second;
        // Not in the indexed list: return any surplus pattern. We pick
        // the first one. This is deterministic; users who want the
        // strongest obliviousness property should randomize encoding
        // via a separate builder helper (not yet implemented).
        if (indexed_.size() < alphabet_size) {
            return indexed_.size();
        }
        // No surplus (k == 2^M) and v is not in the indexed list: the
        // codec was built dense-style but the caller violated the
        // alphabet contract.
        throw std::out_of_range("padded_codec::encode: value not indexed and no surplus");
    }

    [[nodiscard]] V decode(uint64_t pattern) const {
        uint64_t p = pattern & (alphabet_size - 1);
        if (p < indexed_.size()) return indexed_[p];
        return default_;
    }

    [[nodiscard]] const std::vector<V>& indexed() const noexcept { return indexed_; }
    [[nodiscard]] const V& default_value() const noexcept { return default_; }

    // A pattern that decodes to the default value. Retrieval builders
    // can use this to fill unused slots so that non-members whose
    // query lands in an unused slot decode to default. For padded_codec,
    // any pattern in [k, 2^M) works; we pick k (the first surplus).
    // Requires at least one surplus pattern (indexed.size() < 2^M).
    [[nodiscard]] uint64_t default_pattern() const {
        if (indexed_.size() < alphabet_size) return indexed_.size();
        // No surplus: the codec is dense. If default_ is indexed, return
        // its index; otherwise throw since there's no representable
        // default pattern.
        auto it = to_index_.find(default_);
        if (it != to_index_.end()) return it->second;
        throw std::out_of_range("padded_codec::default_pattern: no surplus and default not indexed");
    }

    // Expected probability that a non-member decodes to `v`.
    [[nodiscard]] double nonmember_probability(const V& v) const {
        uint64_t count = 0;
        for (const auto& x : indexed_) if (x == v) ++count;
        if (v == default_) count += alphabet_size - indexed_.size();
        return static_cast<double>(count) / static_cast<double>(alphabet_size);
    }

private:
    std::vector<V> indexed_{};
    V default_{};
    std::unordered_map<V, uint64_t> to_index_{};

    void build_reverse_map() {
        to_index_.reserve(indexed_.size());
        for (uint64_t i = 0; i < indexed_.size(); ++i) {
            to_index_.emplace(indexed_[i], i);
        }
    }
};

} // namespace maph
