/**
 * @file prefix_codec.hpp
 * @brief Prefix-free (Huffman-style) codec.
 *
 * Each logical value v in the alphabet V is assigned a prefix codeword
 * c_v of length l_v <= M, with the prefix-free property: no codeword
 * is a prefix of another. The set of M-bit patterns that begin with
 * c_v is "the class of v": its size is 2^(M - l_v). For a pattern,
 * decoding finds the (unique) v whose codeword is a prefix.
 *
 * Compared to padded_codec, prefix_codec gives finer control over the
 * relative class sizes:
 *
 *   |class(v)| / 2^M  =  2^(-l_v)
 *
 * Choosing codeword lengths to match a target frequency distribution
 * (Huffman) makes the codespace allocation match the distribution
 * itself. For an enclosing ribbon retrieval, this is the lever that
 * controls the non-member output distribution. Frequent values get
 * short codewords + large classes + correspondingly larger probability
 * mass for non-member queries.
 *
 * Kraft's inequality must hold over the supplied codeword lengths:
 *   sum 2^(-l_v) <= 1
 * If the inequality is strict, some patterns decode to the codec's
 * `default_value` (similar to padded_codec's surplus handling).
 *
 * Lengths must be in [1, M]. Codewords are stored canonically
 * (left-aligned in the M-bit pattern, low-order bits zero) but
 * decoding works for any pattern: it scans for the matching prefix.
 */

#pragma once

#include "../concepts/codec.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace maph {

template <typename V, unsigned M>
    requires (M >= 1 && M <= 32)
class prefix_codec {
public:
    using logical_value = V;
    static constexpr unsigned value_bits = M;
    static constexpr uint64_t alphabet_size = uint64_t{1} << M;

    // A code entry: value, prefix bits (left-aligned, low bits zero), and length.
    struct entry {
        V value;
        uint32_t prefix_left_aligned;  // top length_ bits are the codeword
        unsigned length;
    };

    prefix_codec() = default;

    // codes: list of (value, codeword_length) pairs. Library assigns
    // canonical codewords in lex order (Huffman-style canonical code).
    // default_value: receives any patterns left over after Kraft slack.
    prefix_codec(std::vector<std::pair<V, unsigned>> codes, V default_value)
        : default_(std::move(default_value)) {
        validate_and_assign(std::move(codes));
    }

    // Convenience: build with codes derived from a frequency map. Uses
    // a simple Huffman algorithm; ties broken by lex order on V.
    static prefix_codec from_frequencies(
        std::vector<std::pair<V, double>> freqs, V default_value)
    {
        return prefix_codec{huffman_lengths(std::move(freqs), M),
                            std::move(default_value)};
    }

    // ===== codec interface =====

    [[nodiscard]] uint64_t encode(const V& v) const {
        auto it = to_entry_.find(v);
        if (it != to_entry_.end()) {
            return entries_[it->second].prefix_left_aligned;
        }
        // v not in the indexed alphabet: encode with the default's
        // canonical pattern if it has one, else the first surplus.
        auto dit = to_entry_.find(default_);
        if (dit != to_entry_.end()) {
            return entries_[dit->second].prefix_left_aligned;
        }
        // No entry for default either: return surplus_start_, which by
        // construction decodes to the default.
        return surplus_start_;
    }

    [[nodiscard]] V decode(uint64_t pattern) const {
        uint32_t p = static_cast<uint32_t>(pattern & (alphabet_size - 1));
        // Linear scan over entries. For small alphabets this is fine
        // and avoids the complexity of a code tree. Entries are sorted
        // by descending length so longer codewords match first.
        for (const auto& e : entries_sorted_by_length_) {
            uint32_t mask = mask_for_length(e.length);
            if ((p & mask) == (e.prefix_left_aligned & mask)) {
                return e.value;
            }
        }
        return default_;
    }

    // Return the canonical pattern (encode result) and the size of v's
    // codespace class. Useful for randomized encoding and analysis.
    [[nodiscard]] std::pair<uint64_t, uint64_t> class_for(const V& v) const {
        auto it = to_entry_.find(v);
        if (it == to_entry_.end()) {
            return {0, 0};
        }
        const entry& e = entries_[it->second];
        return {e.prefix_left_aligned,
                uint64_t{1} << (M - e.length)};
    }

    // Pick a uniform random pattern from v's class. Useful for cipher-map
    // style obliviousness (stored patterns spread across the class).
    template <typename Rng>
    [[nodiscard]] uint64_t encode_random(const V& v, Rng& rng) const {
        auto [base, count] = class_for(v);
        if (count <= 1) return base;
        std::uniform_int_distribution<uint64_t> dist(0, count - 1);
        return base | dist(rng);
    }

    [[nodiscard]] const V& default_value() const noexcept { return default_; }
    [[nodiscard]] const std::vector<entry>& entries() const noexcept { return entries_; }

    // Probability mass that a uniform M-bit pattern decodes to v.
    // Equals |class(v)| / 2^M = 2^(-length).
    [[nodiscard]] double codespace_share(const V& v) const {
        auto it = to_entry_.find(v);
        if (it == to_entry_.end()) {
            // Default absorbs the surplus.
            if (v == default_) return surplus_share();
            return 0.0;
        }
        const entry& e = entries_[it->second];
        double share = std::ldexp(1.0, -static_cast<int>(e.length));
        if (v == default_) share += surplus_share();
        return share;
    }

private:
    std::vector<entry> entries_{};
    std::vector<entry> entries_sorted_by_length_{};
    std::unordered_map<V, size_t> to_entry_{};
    V default_{};
    uint64_t surplus_start_{0};
    uint64_t surplus_count_{0};

    static uint32_t mask_for_length(unsigned length) {
        if (length >= M) return static_cast<uint32_t>(alphabet_size - 1);
        return static_cast<uint32_t>(((alphabet_size - 1) >> (M - length))
                                     << (M - length));
    }

    [[nodiscard]] double surplus_share() const {
        return static_cast<double>(surplus_count_) / static_cast<double>(alphabet_size);
    }

    void validate_and_assign(std::vector<std::pair<V, unsigned>> codes) {
        if (codes.empty()) {
            throw std::invalid_argument("prefix_codec: at least one code required");
        }
        // Kraft check.
        double k = 0.0;
        for (const auto& [v, l] : codes) {
            if (l < 1 || l > M) {
                throw std::invalid_argument("prefix_codec: code length out of [1, M]");
            }
            k += std::ldexp(1.0, -static_cast<int>(l));
        }
        if (k > 1.0 + 1e-9) {
            throw std::invalid_argument("prefix_codec: Kraft inequality violated");
        }

        // Canonical assignment: sort by (length, value-stable-key) and
        // assign consecutive integers as codewords.
        std::sort(codes.begin(), codes.end(),
            [](const auto& a, const auto& b) {
                if (a.second != b.second) return a.second < b.second;
                return a.first < b.first;
            });

        entries_.reserve(codes.size());
        uint64_t next_code = 0;
        unsigned last_length = 0;
        for (const auto& [v, l] : codes) {
            if (last_length != 0 && l > last_length) {
                next_code <<= (l - last_length);
            }
            uint64_t code = next_code;
            uint32_t aligned = static_cast<uint32_t>(code << (M - l));
            size_t idx = entries_.size();
            entries_.push_back({v, aligned, l});
            to_entry_[v] = idx;
            next_code = code + 1;
            last_length = l;
        }

        // After all codes assigned, if Kraft sum < 1, mark the unused
        // tail of the codespace.
        if (last_length < M) {
            next_code <<= (M - last_length);
        }
        surplus_start_ = next_code;
        surplus_count_ = (next_code <= alphabet_size)
            ? alphabet_size - next_code
            : 0;

        // For decode: longest codewords first.
        entries_sorted_by_length_ = entries_;
        std::sort(entries_sorted_by_length_.begin(),
                  entries_sorted_by_length_.end(),
            [](const entry& a, const entry& b) { return a.length > b.length; });
    }

    static std::vector<std::pair<V, unsigned>>
    huffman_lengths(std::vector<std::pair<V, double>> freqs, unsigned max_len) {
        // Tiny Huffman implementation. For |V| up to a few hundred this
        // is perfectly fine. For larger alphabets, replace with
        // package-merge or similar.
        struct node {
            double freq;
            int left = -1, right = -1;
            V value{};
            bool is_leaf = false;
        };
        std::vector<node> nodes;
        nodes.reserve(2 * freqs.size());
        for (auto& [v, f] : freqs) {
            nodes.push_back({f, -1, -1, v, true});
        }
        // Build tree by repeated merging of two smallest.
        std::vector<int> active;
        for (int i = 0; i < (int)nodes.size(); ++i) active.push_back(i);
        while (active.size() > 1) {
            std::sort(active.begin(), active.end(),
                [&](int a, int b) { return nodes[a].freq > nodes[b].freq; });
            int a = active.back(); active.pop_back();
            int b = active.back(); active.pop_back();
            int idx = static_cast<int>(nodes.size());
            nodes.push_back({nodes[a].freq + nodes[b].freq, a, b, V{}, false});
            active.push_back(idx);
        }

        // Walk tree to compute leaf depths.
        std::vector<std::pair<V, unsigned>> out;
        out.reserve(freqs.size());
        if (nodes.size() == 1) {
            // Single value: length 1.
            out.push_back({nodes[0].value, 1});
            return out;
        }
        struct frame { int idx; unsigned depth; };
        std::vector<frame> stack{{active[0], 0}};
        while (!stack.empty()) {
            frame f = stack.back();
            stack.pop_back();
            const node& n = nodes[f.idx];
            if (n.is_leaf) {
                unsigned len = std::max<unsigned>(1, f.depth);
                if (len > max_len) len = max_len;
                out.push_back({n.value, len});
            } else {
                stack.push_back({n.left, f.depth + 1});
                stack.push_back({n.right, f.depth + 1});
            }
        }
        return out;
    }
};

} // namespace maph
