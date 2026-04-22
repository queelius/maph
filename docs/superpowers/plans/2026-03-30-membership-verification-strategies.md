# Membership Verification Strategies Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement four membership verification strategies (compact packed array, xor filter, ribbon retrieval, configurable wrapper), benchmark them at scale (1M-100M keys), and identify the most efficient default for production perfect hashers.

**Architecture:** Each strategy is a standalone class in `include/maph/membership.hpp` with a common interface (`build`, `verify`, `bits_per_key`, `serialize`, `deserialize`). A benchmark binary exercises all strategies against a shared RecSplit hasher at multiple key counts and fingerprint widths, outputting a TSV comparison table.

**Tech Stack:** C++23, Catch2 v3, RecSplit from `hashers_perfect.hpp`, CMake

## File Structure

| File | Responsibility |
|------|---------------|
| `include/maph/membership.hpp` | All four strategy implementations |
| `tests/v3/test_membership.cpp` | Correctness tests for all strategies |
| `benchmarks/bench_membership.cpp` | Comparative benchmark binary |
| `tests/v3/CMakeLists.txt` | New test target (modify) |
| `benchmarks/CMakeLists.txt` | New benchmark target (modify) |

---

### Task 1: Packed Fingerprint Array + Test Infrastructure

**Files:**
- Create: `include/maph/membership.hpp`
- Create: `tests/v3/test_membership.cpp`
- Modify: `tests/v3/CMakeLists.txt`

- [ ] **Step 1: Write failing test file**

Create `tests/v3/test_membership.cpp`:

```cpp
/**
 * @file test_membership.cpp
 * @brief Tests for membership verification strategies
 */

#include <catch2/catch_test_macros.hpp>
#include <maph/membership.hpp>
#include <maph/hashers_perfect.hpp>
#include <random>
#include <algorithm>
#include <cmath>

using namespace maph;

namespace {

std::vector<std::string> make_keys(size_t count, uint64_t seed = 42) {
    std::vector<std::string> keys;
    keys.reserve(count);
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<int> char_dist('a', 'z');
    std::uniform_int_distribution<size_t> len_dist(4, 16);
    for (size_t i = 0; i < count; ++i) {
        std::string key;
        size_t len = len_dist(rng);
        key.reserve(len);
        for (size_t j = 0; j < len; ++j) {
            key += static_cast<char>(char_dist(rng));
        }
        keys.push_back(std::move(key));
    }
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    return keys;
}

std::vector<std::string> make_unknowns(size_t count, uint64_t seed = 99999) {
    std::vector<std::string> keys;
    keys.reserve(count);
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<int> char_dist('a', 'z');
    for (size_t i = 0; i < count; ++i) {
        std::string key = "UNKNOWN_";
        for (size_t j = 0; j < 12; ++j) {
            key += static_cast<char>(char_dist(rng));
        }
        keys.push_back(std::move(key));
    }
    return keys;
}

// Helper: build a recsplit hasher and return a slot_for lambda
struct recsplit_fixture {
    recsplit8 hasher;
    std::function<std::optional<size_t>(std::string_view)> slot_fn;

    static recsplit_fixture create(const std::vector<std::string>& keys) {
        auto h = recsplit8::builder{}.add_all(keys).build().value();
        auto fn = [h = std::make_shared<recsplit8>(std::move(h))](std::string_view k) -> std::optional<size_t> {
            auto s = h->slot_for(k);
            return s ? std::optional<size_t>{s->value} : std::nullopt;
        };
        // Need to reconstruct since we moved h into the lambda
        auto h2 = recsplit8::builder{}.add_all(keys).build().value();
        return {std::move(h2), std::move(fn)};
    }
};

} // namespace

// ===== PACKED FINGERPRINT ARRAY TESTS =====

TEST_CASE("packed_fingerprint_array: all widths verify known keys", "[membership][packed]") {
    auto keys = make_keys(500);
    auto fix = recsplit_fixture::create(keys);

    SECTION("8-bit") {
        packed_fingerprint_array<8> pfa;
        pfa.build(keys, fix.slot_fn, keys.size());
        for (const auto& key : keys) {
            auto slot = fix.slot_fn(key);
            REQUIRE(slot.has_value());
            REQUIRE(pfa.verify(key, *slot));
        }
        REQUIRE(pfa.bits_per_key(keys.size()) == 8.0);
    }

    SECTION("16-bit") {
        packed_fingerprint_array<16> pfa;
        pfa.build(keys, fix.slot_fn, keys.size());
        for (const auto& key : keys) {
            auto slot = fix.slot_fn(key);
            REQUIRE(slot.has_value());
            REQUIRE(pfa.verify(key, *slot));
        }
        REQUIRE(pfa.bits_per_key(keys.size()) == 16.0);
    }

    SECTION("32-bit") {
        packed_fingerprint_array<32> pfa;
        pfa.build(keys, fix.slot_fn, keys.size());
        for (const auto& key : keys) {
            auto slot = fix.slot_fn(key);
            REQUIRE(slot.has_value());
            REQUIRE(pfa.verify(key, *slot));
        }
        REQUIRE(pfa.bits_per_key(keys.size()) == 32.0);
    }
}

TEST_CASE("packed_fingerprint_array: FP rate within statistical bounds", "[membership][packed]") {
    auto keys = make_keys(1000);
    auto unknowns = make_unknowns(100000);
    auto fix = recsplit_fixture::create(keys);

    packed_fingerprint_array<16> pfa;
    pfa.build(keys, fix.slot_fn, keys.size());

    size_t false_positives = 0;
    for (const auto& uk : unknowns) {
        size_t arbitrary_slot = fix.hasher.hash(uk).value % keys.size();
        if (pfa.verify(uk, arbitrary_slot)) ++false_positives;
    }

    double fp_rate = static_cast<double>(false_positives) / unknowns.size();
    double expected = 1.0 / 65536.0;
    double n = static_cast<double>(unknowns.size());
    double stddev = std::sqrt(expected * (1.0 - expected) / n);

    INFO("FP rate: " << fp_rate << ", expected: " << expected << " +/- " << (3 * stddev));
    REQUIRE(fp_rate < expected + 3 * stddev);
}

TEST_CASE("packed_fingerprint_array: serialization round-trip", "[membership][packed]") {
    auto keys = make_keys(200);
    auto fix = recsplit_fixture::create(keys);

    packed_fingerprint_array<16> original;
    original.build(keys, fix.slot_fn, keys.size());

    auto bytes = original.serialize();
    auto restored = packed_fingerprint_array<16>::deserialize(bytes);
    REQUIRE(restored.has_value());

    for (const auto& key : keys) {
        auto slot = fix.slot_fn(key);
        REQUIRE(slot.has_value());
        REQUIRE(restored->verify(key, *slot));
    }
}

TEST_CASE("packed_fingerprint_array: edge cases", "[membership][packed]") {
    SECTION("Single key") {
        std::vector<std::string> keys = {"only_key"};
        auto fix = recsplit_fixture::create(keys);
        packed_fingerprint_array<8> pfa;
        pfa.build(keys, fix.slot_fn, keys.size());
        REQUIRE(pfa.verify("only_key", fix.slot_fn("only_key").value()));
    }

    SECTION("Duplicate keys deduplicated by caller") {
        // Caller is expected to deduplicate; we just verify no crash
        std::vector<std::string> keys = {"aaa", "bbb", "aaa", "ccc"};
        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
        auto fix = recsplit_fixture::create(keys);
        packed_fingerprint_array<8> pfa;
        pfa.build(keys, fix.slot_fn, keys.size());
        for (const auto& key : keys) {
            REQUIRE(pfa.verify(key, fix.slot_fn(key).value()));
        }
    }

    SECTION("Very long keys") {
        std::vector<std::string> keys;
        for (int i = 0; i < 10; ++i) {
            keys.push_back(std::string(1000, 'a' + i));
        }
        auto fix = recsplit_fixture::create(keys);
        packed_fingerprint_array<16> pfa;
        pfa.build(keys, fix.slot_fn, keys.size());
        for (const auto& key : keys) {
            REQUIRE(pfa.verify(key, fix.slot_fn(key).value()));
        }
    }
}
```

- [ ] **Step 2: Create membership.hpp with packed_fingerprint_array**

Create `include/maph/membership.hpp`:

```cpp
/**
 * @file membership.hpp
 * @brief Membership verification strategies for perfect hash functions
 *
 * Provides multiple strategies for verifying that a queried key belongs
 * to the original build set. Each strategy offers a different tradeoff
 * between space, query speed, and false positive rate.
 */

#pragma once

#include "core.hpp"
#include <vector>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <optional>
#include <functional>
#include <random>
#include <algorithm>
#include <bit>
#include <cmath>

namespace maph {

// ===== COMMON FINGERPRINT HASH =====

// Independent hash for fingerprinting (must differ from perfect hash internals).
// SplitMix64 finalization applied to FNV-1a.
inline uint64_t membership_fingerprint(std::string_view key) noexcept {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (unsigned char c : key) {
        h ^= c;
        h *= 0x100000001b3ULL;
    }
    h ^= h >> 30;
    h *= 0xbf58476d1ce4e5b9ULL;
    h ^= h >> 27;
    h *= 0x94d049bb133111ebULL;
    h ^= h >> 31;
    return h;
}

// ===== STRATEGY 1: COMPACT PACKED FINGERPRINT ARRAY =====

/**
 * @class packed_fingerprint_array
 * @brief Stores k-bit fingerprints in a tightly packed bit array
 *
 * Space: exactly FingerprintBits bits per key.
 * Query: extract k-bit fingerprint from packed array, compare.
 * FP rate: 2^-FingerprintBits.
 *
 * @tparam FingerprintBits Width of each fingerprint (8, 16, or 32)
 */
template<unsigned FingerprintBits>
    requires (FingerprintBits == 8 || FingerprintBits == 16 || FingerprintBits == 32)
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
```

- [ ] **Step 3: Add test_v3_membership target to tests/v3/CMakeLists.txt**

After the `test_v3_perfect_hash_extended` target block (around line 213), add:

```cmake
# Membership verification strategy tests
add_executable(test_v3_membership
    test_membership.cpp
)

target_link_libraries(test_v3_membership
    PRIVATE
    maph
    Catch2::Catch2WithMain
    pthread
)

target_include_directories(test_v3_membership
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(test_v3_membership PRIVATE cxx_std_${V3_CXX_STANDARD})
```

In the `V3_TEST_TARGETS` list (around line 243), add `test_v3_membership`.

In the `catch_discover_tests` section (inside the `if(EXISTS ...)` block), add:

```cmake
    catch_discover_tests(test_v3_membership
        TEST_PREFIX "v3_membership:"
        PROPERTIES TIMEOUT 300
    )
```

- [ ] **Step 4: Build and run**

```bash
cd /home/spinoza/github/released/maph/build && cmake .. -DBUILD_TESTS=ON && make -j$(nproc) test_v3_membership 2>&1 | tail -10
./tests/v3/test_v3_membership "[packed]" --reporter compact
```

Expected: All packed_fingerprint_array tests PASS.

- [ ] **Step 5: Commit**

```bash
git add include/maph/membership.hpp tests/v3/test_membership.cpp tests/v3/CMakeLists.txt
git commit -m "feat: add packed_fingerprint_array membership verification strategy

Implements Strategy 1: k-bit fingerprints (8/16/32) in a tightly
packed bit array indexed by perfect hash slot. Exactly k bits/key.
Includes serialization and comprehensive tests."
```

---

### Task 2: Xor Filter

**Files:**
- Modify: `include/maph/membership.hpp`
- Modify: `tests/v3/test_membership.cpp`

- [ ] **Step 1: Write failing tests for xor_filter**

Append to `tests/v3/test_membership.cpp`:

```cpp
// ===== XOR FILTER TESTS =====

TEST_CASE("xor_filter: all widths verify known keys", "[membership][xor]") {
    auto keys = make_keys(1000);

    SECTION("8-bit") {
        xor_filter<8> xf;
        REQUIRE(xf.build(keys));
        for (const auto& key : keys) { REQUIRE(xf.verify(key)); }
        double bpk = xf.bits_per_key(keys.size());
        INFO("8-bit bits/key: " << bpk);
        REQUIRE(bpk > 7.0);
        REQUIRE(bpk < 12.0);  // ~1.23 * 8 = 9.84
    }

    SECTION("16-bit") {
        xor_filter<16> xf;
        REQUIRE(xf.build(keys));
        for (const auto& key : keys) { REQUIRE(xf.verify(key)); }
        double bpk = xf.bits_per_key(keys.size());
        INFO("16-bit bits/key: " << bpk);
        REQUIRE(bpk > 14.0);
        REQUIRE(bpk < 22.0);
    }

    SECTION("32-bit") {
        xor_filter<32> xf;
        REQUIRE(xf.build(keys));
        for (const auto& key : keys) { REQUIRE(xf.verify(key)); }
    }
}

TEST_CASE("xor_filter: FP rate within statistical bounds", "[membership][xor]") {
    auto keys = make_keys(1000);
    auto unknowns = make_unknowns(100000);

    xor_filter<16> xf;
    REQUIRE(xf.build(keys));

    size_t fps = 0;
    for (const auto& uk : unknowns) { if (xf.verify(uk)) ++fps; }

    double fp_rate = static_cast<double>(fps) / unknowns.size();
    double expected = 1.0 / 65536.0;
    double stddev = std::sqrt(expected * (1.0 - expected) / static_cast<double>(unknowns.size()));
    INFO("FP rate: " << fp_rate << ", expected: " << expected << " +/- " << (3 * stddev));
    REQUIRE(fp_rate < expected + 3 * stddev);
}

TEST_CASE("xor_filter: serialization round-trip", "[membership][xor]") {
    auto keys = make_keys(500);
    xor_filter<16> original;
    REQUIRE(original.build(keys));

    auto bytes = original.serialize();
    auto restored = xor_filter<16>::deserialize(bytes);
    REQUIRE(restored.has_value());
    for (const auto& key : keys) { REQUIRE(restored->verify(key)); }
}

TEST_CASE("xor_filter: edge cases", "[membership][xor]") {
    SECTION("Small key set (3 keys)") {
        std::vector<std::string> keys = {"alpha", "beta", "gamma"};
        xor_filter<8> xf;
        REQUIRE(xf.build(keys));
        for (const auto& key : keys) { REQUIRE(xf.verify(key)); }
    }

    SECTION("Very long keys") {
        std::vector<std::string> keys;
        for (int i = 0; i < 20; ++i) keys.push_back(std::string(1000, 'a' + i));
        xor_filter<16> xf;
        REQUIRE(xf.build(keys));
        for (const auto& key : keys) { REQUIRE(xf.verify(key)); }
    }
}
```

- [ ] **Step 2: Verify tests fail (compilation error)**

```bash
cd /home/spinoza/github/released/maph/build && make -j$(nproc) test_v3_membership 2>&1 | grep "error:"
```

Expected: `error: 'xor_filter' was not declared`

- [ ] **Step 3: Implement xor_filter in membership.hpp**

Add before the closing `} // namespace maph` in `include/maph/membership.hpp`:

```cpp
// ===== STRATEGY 2: XOR FILTER =====

/**
 * @class xor_filter
 * @brief 3-wise xor filter for membership testing
 *
 * Space: ~1.23 * FingerprintBits bits per key.
 * Query: 3 memory accesses + XOR + compare.
 * FP rate: 2^-FingerprintBits.
 *
 * Construction uses the "peeling" algorithm on a 3-partite hypergraph.
 * Retries with a new seed if peeling fails (~3% per attempt).
 *
 * @tparam FingerprintBits Width of each fingerprint (8, 16, or 32)
 */
template<unsigned FingerprintBits>
    requires (FingerprintBits == 8 || FingerprintBits == 16 || FingerprintBits == 32)
class xor_filter {
    using fp_type = std::conditional_t<FingerprintBits <= 8, uint8_t,
                    std::conditional_t<FingerprintBits <= 16, uint16_t, uint32_t>>;

    static constexpr uint64_t fp_mask = (1ULL << FingerprintBits) - 1;

    std::vector<fp_type> table_;
    size_t segment_size_{0};
    uint64_t seed_{0};

    struct key_hashes {
        size_t h0, h1, h2;
        fp_type fingerprint;
    };

    key_hashes hash_key(std::string_view key) const noexcept {
        uint64_t h = membership_fingerprint(key) ^ seed_;

        // Second independent hash via additional mixing
        uint64_t h2 = h;
        h2 ^= h2 >> 33;
        h2 *= 0xff51afd7ed558ccdULL;
        h2 ^= h2 >> 33;
        h2 *= 0xc4ceb9fe1a85ec53ULL;
        h2 ^= h2 >> 33;

        auto fp = static_cast<fp_type>(h2 & fp_mask);
        if (fp == 0) fp = 1;  // Non-zero fingerprint for correctness

        return {
            static_cast<size_t>(h % segment_size_),
            static_cast<size_t>((h >> 21) % segment_size_) + segment_size_,
            static_cast<size_t>((h2 >> 11) % segment_size_) + 2 * segment_size_,
            fp
        };
    }

public:
    xor_filter() = default;

    bool build(const std::vector<std::string>& keys) {
        if (keys.empty()) return false;

        size_t n = keys.size();
        segment_size_ = std::max(size_t{4}, static_cast<size_t>(std::ceil(n * 1.23 / 3.0)));
        size_t table_size = 3 * segment_size_;

        std::mt19937_64 rng{42};

        for (int attempt = 0; attempt < 100; ++attempt) {
            seed_ = rng();
            table_.assign(table_size, 0);

            // Compute hashes and build degree/xor tracking
            std::vector<int32_t> degree(table_size, 0);
            std::vector<uint64_t> deg_xor(table_size, 0);
            std::vector<key_hashes> hashes(n);

            for (size_t i = 0; i < n; ++i) {
                hashes[i] = hash_key(keys[i]);
                degree[hashes[i].h0]++;
                degree[hashes[i].h1]++;
                degree[hashes[i].h2]++;
                deg_xor[hashes[i].h0] ^= i;
                deg_xor[hashes[i].h1] ^= i;
                deg_xor[hashes[i].h2] ^= i;
            }

            // Peeling: repeatedly remove degree-1 slots
            struct peel_entry { size_t key_idx; size_t slot; };
            std::vector<peel_entry> stack;
            stack.reserve(n);

            std::vector<size_t> queue;
            for (size_t i = 0; i < table_size; ++i) {
                if (degree[i] == 1) queue.push_back(i);
            }

            while (!queue.empty()) {
                size_t pos = queue.back();
                queue.pop_back();
                if (degree[pos] != 1) continue;

                size_t ki = static_cast<size_t>(deg_xor[pos]);
                if (ki >= n) continue;

                stack.push_back({ki, pos});

                const auto& kh = hashes[ki];
                for (size_t slot : {kh.h0, kh.h1, kh.h2}) {
                    degree[slot]--;
                    deg_xor[slot] ^= ki;
                    if (degree[slot] == 1) queue.push_back(slot);
                }
            }

            if (stack.size() != n) continue;  // Peeling failed, retry

            // Assign values in reverse peel order
            for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
                const auto& kh = hashes[it->key_idx];
                fp_type val = kh.fingerprint ^ table_[kh.h0] ^ table_[kh.h1] ^ table_[kh.h2];
                table_[it->slot] = val;
            }
            return true;
        }
        return false;
    }

    [[nodiscard]] bool verify(std::string_view key) const noexcept {
        if (table_.empty()) return false;
        auto kh = hash_key(key);
        return (table_[kh.h0] ^ table_[kh.h1] ^ table_[kh.h2]) == kh.fingerprint;
    }

    [[nodiscard]] double bits_per_key(size_t key_count) const noexcept {
        return key_count > 0 ? static_cast<double>(table_.size() * FingerprintBits) / key_count : 0.0;
    }

    [[nodiscard]] size_t memory_bytes() const noexcept {
        return table_.size() * sizeof(fp_type);
    }

    [[nodiscard]] std::vector<std::byte> serialize() const {
        std::vector<std::byte> out;
        auto append = [&](const auto& val) {
            auto bytes = std::bit_cast<std::array<std::byte, sizeof(val)>>(val);
            out.insert(out.end(), bytes.begin(), bytes.end());
        };
        append(static_cast<uint32_t>(FingerprintBits));
        append(seed_);
        append(static_cast<uint64_t>(segment_size_));
        append(static_cast<uint64_t>(table_.size()));
        for (auto v : table_) append(v);
        return out;
    }

    [[nodiscard]] static std::optional<xor_filter> deserialize(std::span<const std::byte> bytes) {
        size_t off = 0;
        auto read = [&](auto& val) -> bool {
            if (off + sizeof(val) > bytes.size()) return false;
            std::memcpy(&val, bytes.data() + off, sizeof(val));
            off += sizeof(val);
            return true;
        };
        uint32_t fp_bits{}; uint64_t seed{}, seg{}, tsize{};
        if (!read(fp_bits) || fp_bits != FingerprintBits) return std::nullopt;
        if (!read(seed) || !read(seg) || !read(tsize)) return std::nullopt;
        if (tsize > (bytes.size() - off) / sizeof(fp_type)) return std::nullopt;

        xor_filter r;
        r.seed_ = seed;
        r.segment_size_ = static_cast<size_t>(seg);
        r.table_.resize(static_cast<size_t>(tsize));
        for (auto& v : r.table_) { if (!read(v)) return std::nullopt; }
        return r;
    }
};
```

- [ ] **Step 4: Build and run xor tests**

```bash
cd /home/spinoza/github/released/maph/build && make -j$(nproc) test_v3_membership && ./tests/v3/test_v3_membership "[xor]" --reporter compact
```

Expected: All xor_filter tests PASS.

- [ ] **Step 5: Commit**

```bash
git add include/maph/membership.hpp tests/v3/test_membership.cpp
git commit -m "feat: add xor_filter membership verification strategy

3-wise xor filter with peeling construction.
Space: ~1.23 * k bits/key. Query: 3 memory accesses + XOR."
```

---

### Task 3: Ribbon Retrieval

**Files:**
- Modify: `include/maph/membership.hpp`
- Modify: `tests/v3/test_membership.cpp`

- [ ] **Step 1: Write failing tests**

Append to `tests/v3/test_membership.cpp`:

```cpp
// ===== RIBBON FILTER TESTS =====

TEST_CASE("ribbon_filter: all widths verify known keys", "[membership][ribbon]") {
    auto keys = make_keys(500);

    SECTION("8-bit") {
        ribbon_filter<8> rf;
        REQUIRE(rf.build(keys));
        for (const auto& key : keys) { REQUIRE(rf.verify(key)); }
        double bpk = rf.bits_per_key(keys.size());
        INFO("8-bit bits/key: " << bpk);
        REQUIRE(bpk > 7.0);
        REQUIRE(bpk < 11.0);
    }

    SECTION("16-bit") {
        ribbon_filter<16> rf;
        REQUIRE(rf.build(keys));
        for (const auto& key : keys) { REQUIRE(rf.verify(key)); }
        double bpk = rf.bits_per_key(keys.size());
        INFO("16-bit bits/key: " << bpk);
        REQUIRE(bpk > 14.0);
        REQUIRE(bpk < 20.0);
    }

    SECTION("32-bit") {
        ribbon_filter<32> rf;
        REQUIRE(rf.build(keys));
        for (const auto& key : keys) { REQUIRE(rf.verify(key)); }
    }
}

TEST_CASE("ribbon_filter: FP rate within statistical bounds", "[membership][ribbon]") {
    auto keys = make_keys(1000);
    auto unknowns = make_unknowns(100000);

    ribbon_filter<16> rf;
    REQUIRE(rf.build(keys));

    size_t fps = 0;
    for (const auto& uk : unknowns) { if (rf.verify(uk)) ++fps; }

    double fp_rate = static_cast<double>(fps) / unknowns.size();
    double expected = 1.0 / 65536.0;
    double stddev = std::sqrt(expected * (1.0 - expected) / static_cast<double>(unknowns.size()));
    INFO("FP rate: " << fp_rate << ", expected: " << expected << " +/- " << (3 * stddev));
    REQUIRE(fp_rate < expected + 3 * stddev);
}

TEST_CASE("ribbon_filter: serialization round-trip", "[membership][ribbon]") {
    auto keys = make_keys(500);
    ribbon_filter<16> original;
    REQUIRE(original.build(keys));

    auto bytes = original.serialize();
    auto restored = ribbon_filter<16>::deserialize(bytes);
    REQUIRE(restored.has_value());
    for (const auto& key : keys) { REQUIRE(restored->verify(key)); }
}

TEST_CASE("ribbon_filter: edge cases", "[membership][ribbon]") {
    SECTION("Small key set") {
        std::vector<std::string> keys = {"alpha", "beta", "gamma"};
        ribbon_filter<8> rf;
        REQUIRE(rf.build(keys));
        for (const auto& key : keys) { REQUIRE(rf.verify(key)); }
    }

    SECTION("Very long keys") {
        std::vector<std::string> keys;
        for (int i = 0; i < 20; ++i) keys.push_back(std::string(1000, 'a' + i));
        ribbon_filter<16> rf;
        REQUIRE(rf.build(keys));
        for (const auto& key : keys) { REQUIRE(rf.verify(key)); }
    }
}
```

- [ ] **Step 2: Verify tests fail**

```bash
cd /home/spinoza/github/released/maph/build && make -j$(nproc) test_v3_membership 2>&1 | grep "error:"
```

Expected: `error: 'ribbon_filter' was not declared`

- [ ] **Step 3: Implement ribbon_filter**

Add before the closing `} // namespace maph` in `include/maph/membership.hpp`.

The ribbon filter uses a banded linear system of width 64. Each key maps to a row starting at a pseudorandom position with a pseudorandom 64-bit coefficient vector. Construction does forward elimination (sorted by start position) then back-substitution. The stored solution lets us retrieve an r-bit fingerprint for any key in the set.

```cpp
// ===== STRATEGY 3: RIBBON RETRIEVAL =====

/**
 * @class ribbon_filter
 * @brief Homogeneous ribbon retrieval for membership testing
 *
 * Space: ~FingerprintBits * (1 + epsilon) bits per key (epsilon ~3-8%).
 * Query: hash to starting row + XOR chain over ~64 solution entries.
 * FP rate: 2^-FingerprintBits.
 *
 * Uses a banded matrix with bandwidth w=64. Construction: sort rows by
 * start position, forward Gaussian elimination, back-substitution.
 *
 * @tparam FingerprintBits Width of stored fingerprint (8, 16, or 32)
 */
template<unsigned FingerprintBits>
    requires (FingerprintBits == 8 || FingerprintBits == 16 || FingerprintBits == 32)
class ribbon_filter {
    using fp_type = std::conditional_t<FingerprintBits <= 8, uint8_t,
                    std::conditional_t<FingerprintBits <= 16, uint16_t, uint32_t>>;

    static constexpr uint64_t fp_mask = (1ULL << FingerprintBits) - 1;
    static constexpr size_t W = 64;  // Band width = machine word

    std::vector<fp_type> solution_;  // One fp_type per row
    size_t num_rows_{0};
    uint64_t seed_{0};

    // Each key produces a row: starting position, 64-bit coefficients, desired result
    struct row {
        size_t start;
        uint64_t coeffs;  // Bit i means column (start + i) participates
        fp_type result;
    };

    row make_row(std::string_view key) const noexcept {
        uint64_t h = membership_fingerprint(key) ^ seed_;

        size_t start = 0;
        if (num_rows_ > W) {
            start = static_cast<size_t>((h >> 32) % (num_rows_ - W + 1));
        }

        // Coefficients from a second mix
        uint64_t c = h * 0xbf58476d1ce4e5b9ULL;
        c ^= c >> 31;
        c |= 1ULL;  // Ensure at least bit 0 is set (non-zero row)

        // Fingerprint from a third mix
        uint64_t fh = h * 0x9e3779b97f4a7c15ULL;
        fh ^= fh >> 30;
        auto fp = static_cast<fp_type>(fh & fp_mask);

        return {start, c, fp};
    }

    // Query: compute XOR of solution[start+i] for each set bit i in coeffs
    fp_type query_row(const row& r) const noexcept {
        fp_type result = 0;
        uint64_t c = r.coeffs;
        size_t base = r.start;
        while (c != 0) {
            size_t bit = static_cast<size_t>(std::countr_zero(c));  // Lowest set bit
            result ^= solution_[base + bit];
            c &= c - 1;  // Clear lowest set bit
        }
        return result;
    }

public:
    ribbon_filter() = default;

    bool build(const std::vector<std::string>& keys) {
        if (keys.empty()) return false;
        size_t n = keys.size();
        std::mt19937_64 rng{42};

        for (int attempt = 0; attempt < 50; ++attempt) {
            seed_ = rng();
            num_rows_ = n + std::max(size_t{W}, static_cast<size_t>(n * 0.08));

            // Build rows, sorted by start position
            std::vector<row> rows(n);
            for (size_t i = 0; i < n; ++i) {
                rows[i] = make_row(keys[i]);
            }
            std::sort(rows.begin(), rows.end(),
                      [](const row& a, const row& b) { return a.start < b.start; });

            // Forward elimination.
            // pivot_coeffs[col] and pivot_result[col] store the pivot row
            // that "owns" column col (if any). A pivot row's leading
            // coefficient is at column col.
            std::vector<uint64_t> pivot_coeffs(num_rows_, 0);
            std::vector<fp_type> pivot_result(num_rows_, 0);
            bool ok = true;

            for (auto& r : rows) {
                uint64_t c = r.coeffs;
                fp_type res = r.result;
                size_t base = r.start;

                while (c != 0) {
                    size_t bit = static_cast<size_t>(std::countr_zero(c));
                    size_t col = base + bit;

                    if (col >= num_rows_) { ok = false; break; }

                    if (pivot_coeffs[col] == 0) {
                        // Claim this column as our pivot.
                        // Shift so that the leading bit aligns with bit 0.
                        pivot_coeffs[col] = c >> bit;
                        pivot_result[col] = res;
                        c = 0;  // Row consumed
                    } else {
                        // Eliminate: XOR with existing pivot, then re-align
                        c ^= pivot_coeffs[col] << bit;
                        res ^= pivot_result[col];
                        // c now has bit `bit` cleared; loop finds next set bit
                    }
                }
                if (!ok) break;
            }

            if (!ok) continue;

            // Back-substitution
            solution_.assign(num_rows_, 0);
            for (size_t col = num_rows_; col-- > 0;) {
                if (pivot_coeffs[col] == 0) continue;  // Free variable, leave as 0

                uint64_t c = pivot_coeffs[col];
                fp_type val = pivot_result[col];

                // XOR in already-solved columns referenced by this pivot.
                // Bit 0 of c is the pivot column itself; higher bits reference
                // columns col+1, col+2, etc.
                uint64_t rest = c >> 1;  // Skip bit 0 (the pivot column)
                size_t offset = 1;
                while (rest != 0) {
                    size_t bit = static_cast<size_t>(std::countr_zero(rest));
                    size_t ref = col + offset + bit;
                    if (ref < num_rows_) {
                        val ^= solution_[ref];
                    }
                    rest >>= (bit + 1);
                    offset += bit + 1;
                }
                solution_[col] = val;
            }

            // Verify all keys
            bool verified = true;
            for (size_t i = 0; i < n; ++i) {
                auto r = make_row(keys[i]);
                if (query_row(r) != r.result) { verified = false; break; }
            }
            if (verified) return true;
        }
        return false;
    }

    [[nodiscard]] bool verify(std::string_view key) const noexcept {
        if (solution_.empty()) return false;
        auto r = make_row(key);
        return query_row(r) == r.result;
    }

    [[nodiscard]] double bits_per_key(size_t key_count) const noexcept {
        return key_count > 0 ? static_cast<double>(solution_.size() * FingerprintBits) / key_count : 0.0;
    }

    [[nodiscard]] size_t memory_bytes() const noexcept {
        return solution_.size() * sizeof(fp_type);
    }

    [[nodiscard]] std::vector<std::byte> serialize() const {
        std::vector<std::byte> out;
        auto append = [&](const auto& val) {
            auto bytes = std::bit_cast<std::array<std::byte, sizeof(val)>>(val);
            out.insert(out.end(), bytes.begin(), bytes.end());
        };
        append(static_cast<uint32_t>(FingerprintBits));
        append(seed_);
        append(static_cast<uint64_t>(num_rows_));
        append(static_cast<uint64_t>(solution_.size()));
        for (auto v : solution_) append(v);
        return out;
    }

    [[nodiscard]] static std::optional<ribbon_filter> deserialize(std::span<const std::byte> bytes) {
        size_t off = 0;
        auto read = [&](auto& val) -> bool {
            if (off + sizeof(val) > bytes.size()) return false;
            std::memcpy(&val, bytes.data() + off, sizeof(val));
            off += sizeof(val);
            return true;
        };
        uint32_t fp_bits{}; uint64_t seed{}, nrows{}, sol_size{};
        if (!read(fp_bits) || fp_bits != FingerprintBits) return std::nullopt;
        if (!read(seed) || !read(nrows) || !read(sol_size)) return std::nullopt;
        if (sol_size > (bytes.size() - off) / sizeof(fp_type)) return std::nullopt;

        ribbon_filter r;
        r.seed_ = seed;
        r.num_rows_ = static_cast<size_t>(nrows);
        r.solution_.resize(static_cast<size_t>(sol_size));
        for (auto& v : r.solution_) { if (!read(v)) return std::nullopt; }
        return r;
    }
};
```

- [ ] **Step 4: Build and run ribbon tests**

```bash
cd /home/spinoza/github/released/maph/build && make -j$(nproc) test_v3_membership && ./tests/v3/test_v3_membership "[ribbon]" --reporter compact
```

Expected: All ribbon_filter tests PASS.

- [ ] **Step 5: Commit**

```bash
git add include/maph/membership.hpp tests/v3/test_membership.cpp
git commit -m "feat: add ribbon_filter membership verification strategy

Homogeneous ribbon retrieval with bandwidth 64. Space: ~r*(1+eps)
bits/key. Banded Gaussian elimination + back-substitution."
```

---

### Task 4: Configurable Fingerprint Verifier

**Files:**
- Modify: `include/maph/membership.hpp`
- Modify: `tests/v3/test_membership.cpp`

- [ ] **Step 1: Write failing tests**

Append to `tests/v3/test_membership.cpp`:

```cpp
// ===== CONFIGURABLE VERIFIER TESTS =====

TEST_CASE("fingerprint_verifier: 16-bit default", "[membership][configurable]") {
    auto keys = make_keys(200);
    auto fix = recsplit_fixture::create(keys);

    fingerprint_verifier<16> fv;
    fv.build(keys, fix.slot_fn, keys.size());

    for (const auto& key : keys) {
        auto slot = fix.slot_fn(key);
        REQUIRE(slot.has_value());
        REQUIRE(fv.verify(key, *slot));
    }
}

TEST_CASE("fingerprint_verifier: disabled (0 bits)", "[membership][configurable]") {
    fingerprint_verifier<0> fv;
    fv.build({}, [](std::string_view) { return std::optional<size_t>{}; }, 0);

    REQUIRE(fv.verify("anything", 0));
    REQUIRE(fv.verify("not_a_real_key", 42));
    REQUIRE(fv.bits_per_key(100) == 0.0);
    REQUIRE(fv.memory_bytes() == 0);
}

TEST_CASE("fingerprint_verifier: serialization round-trip", "[membership][configurable]") {
    auto keys = make_keys(200);
    auto fix = recsplit_fixture::create(keys);

    fingerprint_verifier<16> original;
    original.build(keys, fix.slot_fn, keys.size());

    auto bytes = original.serialize();
    auto restored = fingerprint_verifier<16>::deserialize(bytes);
    REQUIRE(restored.has_value());

    for (const auto& key : keys) {
        auto slot = fix.slot_fn(key);
        REQUIRE(slot.has_value());
        REQUIRE(restored->verify(key, *slot));
    }
}
```

- [ ] **Step 2: Verify tests fail**

```bash
cd /home/spinoza/github/released/maph/build && make -j$(nproc) test_v3_membership 2>&1 | grep "error:"
```

Expected: `error: 'fingerprint_verifier' was not declared`

- [ ] **Step 3: Implement fingerprint_verifier**

Add before the closing `} // namespace maph` in `include/maph/membership.hpp`:

```cpp
// ===== STRATEGY 4: CONFIGURABLE FINGERPRINT VERIFIER =====

/**
 * @class fingerprint_verifier
 * @brief Policy wrapper with configurable fingerprint width
 *
 * Wraps packed_fingerprint_array. Set FingerprintBits=0 to disable
 * verification entirely. This is the integration interface for
 * production perfect hashers.
 *
 * @tparam FingerprintBits 0 (disabled), 8, 16, or 32
 */
template<unsigned FingerprintBits>
    requires (FingerprintBits == 0 || FingerprintBits == 8 || FingerprintBits == 16 || FingerprintBits == 32)
class fingerprint_verifier;

// Specialization: disabled (0 bits)
template<>
class fingerprint_verifier<0> {
public:
    void build(const std::vector<std::string>&,
               std::function<std::optional<size_t>(std::string_view)>,
               size_t) {}

    [[nodiscard]] bool verify(std::string_view, size_t) const noexcept { return true; }
    [[nodiscard]] double bits_per_key(size_t) const noexcept { return 0.0; }
    [[nodiscard]] size_t memory_bytes() const noexcept { return 0; }
    [[nodiscard]] std::vector<std::byte> serialize() const { return {}; }

    [[nodiscard]] static std::optional<fingerprint_verifier> deserialize(std::span<const std::byte>) {
        return fingerprint_verifier{};
    }
};

// Primary template: delegates to packed_fingerprint_array
template<unsigned FingerprintBits>
    requires (FingerprintBits == 8 || FingerprintBits == 16 || FingerprintBits == 32)
class fingerprint_verifier<FingerprintBits> {
    packed_fingerprint_array<FingerprintBits> inner_;

public:
    void build(const std::vector<std::string>& keys,
               std::function<std::optional<size_t>(std::string_view)> slot_for,
               size_t total_slots) {
        inner_.build(keys, slot_for, total_slots);
    }

    [[nodiscard]] bool verify(std::string_view key, size_t slot) const noexcept {
        return inner_.verify(key, slot);
    }

    [[nodiscard]] double bits_per_key(size_t key_count) const noexcept {
        return inner_.bits_per_key(key_count);
    }

    [[nodiscard]] size_t memory_bytes() const noexcept {
        return inner_.memory_bytes();
    }

    [[nodiscard]] std::vector<std::byte> serialize() const { return inner_.serialize(); }

    [[nodiscard]] static std::optional<fingerprint_verifier> deserialize(std::span<const std::byte> bytes) {
        auto inner = packed_fingerprint_array<FingerprintBits>::deserialize(bytes);
        if (!inner) return std::nullopt;
        fingerprint_verifier r;
        r.inner_ = std::move(*inner);
        return r;
    }
};
```

- [ ] **Step 4: Build and run**

```bash
cd /home/spinoza/github/released/maph/build && make -j$(nproc) test_v3_membership && ./tests/v3/test_v3_membership "[configurable]" --reporter compact
```

Expected: All fingerprint_verifier tests PASS.

- [ ] **Step 5: Run full membership test suite**

```bash
./tests/v3/test_v3_membership --reporter compact
```

Expected: All tests PASS across all four strategies.

- [ ] **Step 6: Commit**

```bash
git add include/maph/membership.hpp tests/v3/test_membership.cpp
git commit -m "feat: add fingerprint_verifier configurable wrapper

Policy wrapper (0=disabled, 8, 16, 32 bits). Explicit specialization
for 0-bit case. Delegates to packed_fingerprint_array otherwise."
```

---

### Task 5: Verify No Regressions

**Files:** None (verification only)

- [ ] **Step 1: Run all existing tests**

```bash
cd /home/spinoza/github/released/maph/build && ctest --output-on-failure -R "v3_" 2>&1 | tail -20
```

Expected: All existing tests still PASS (including perfect hash, core, storage, etc.).

---

### Task 6: Benchmark Binary

**Files:**
- Create: `benchmarks/bench_membership.cpp`
- Modify: `benchmarks/CMakeLists.txt`

- [ ] **Step 1: Create bench_membership.cpp**

Create `benchmarks/bench_membership.cpp`:

```cpp
/**
 * @file bench_membership.cpp
 * @brief Comparative benchmark for membership verification strategies
 */

#include <maph/core.hpp>
#include <maph/hashers_perfect.hpp>
#include <maph/membership.hpp>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <random>
#include <algorithm>
#include <numeric>
#include <vector>
#include <string>
#include <functional>

using namespace maph;
using namespace std::chrono;

std::vector<std::string> gen_keys(size_t count, uint64_t seed = 42) {
    std::vector<std::string> keys;
    keys.reserve(count);
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<int> byte_dist(0, 255);
    for (size_t i = 0; i < count; ++i) {
        std::string key(16, '\0');
        for (auto& c : key) c = static_cast<char>(byte_dist(rng));
        keys.push_back(std::move(key));
    }
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    return keys;
}

std::vector<std::string> gen_unknowns(size_t count, uint64_t seed = 99999) {
    std::vector<std::string> keys;
    keys.reserve(count);
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<int> byte_dist(0, 255);
    for (size_t i = 0; i < count; ++i) {
        std::string key = "UNK_";
        for (size_t j = 0; j < 16; ++j) key += static_cast<char>(byte_dist(rng));
        keys.push_back(std::move(key));
    }
    return keys;
}

struct latency_stats {
    double median_ns, p99_ns, avg_ns;
};

template<typename Fn>
latency_stats measure(Fn&& fn, size_t iters) {
    std::vector<double> times;
    times.reserve(iters);
    for (size_t i = 0; i < iters; ++i) {
        auto t0 = high_resolution_clock::now();
        fn(i);
        auto t1 = high_resolution_clock::now();
        times.push_back(static_cast<double>(duration_cast<nanoseconds>(t1 - t0).count()));
    }
    std::sort(times.begin(), times.end());
    return {
        times[times.size() / 2],
        times[static_cast<size_t>(times.size() * 0.99)],
        std::accumulate(times.begin(), times.end(), 0.0) / static_cast<double>(times.size())
    };
}

struct result {
    std::string strategy;
    unsigned fp_bits;
    size_t key_count;
    double bits_per_key;
    double build_ms;
    double q_known_med_ns, q_known_p99_ns;
    double q_unk_med_ns, q_unk_p99_ns;
    double fp_rate;
    size_t mem_bytes;
};

// TSV output (spec says tab-separated)
void print_header() {
    std::cout << "strategy\tfp_bits\tkeys\tbits_per_key\tbuild_ms\t"
              << "q_known_med_ns\tq_known_p99_ns\tq_unk_med_ns\tq_unk_p99_ns\t"
              << "fp_rate\tmem_kb\n";
}

void print_result(const result& r) {
    std::cout << std::fixed
              << r.strategy << '\t' << r.fp_bits << '\t' << r.key_count << '\t'
              << std::setprecision(2) << r.bits_per_key << '\t' << r.build_ms << '\t'
              << r.q_known_med_ns << '\t' << r.q_known_p99_ns << '\t'
              << r.q_unk_med_ns << '\t' << r.q_unk_p99_ns << '\t'
              << std::setprecision(10) << r.fp_rate << '\t'
              << std::setprecision(1) << (r.mem_bytes / 1024.0) << '\n';
}

// === Strategy runners ===

template<unsigned FPBits>
result run_packed(const std::vector<std::string>& keys,
                  const std::vector<std::string>& unknowns,
                  recsplit8& hasher, size_t qi) {
    auto slot_fn = [&](std::string_view k) -> std::optional<size_t> {
        auto s = hasher.slot_for(k);
        return s ? std::optional<size_t>{s->value} : std::nullopt;
    };
    packed_fingerprint_array<FPBits> pfa;
    auto t0 = high_resolution_clock::now();
    pfa.build(keys, slot_fn, keys.size());
    auto t1 = high_resolution_clock::now();

    std::mt19937_64 rng{123};
    std::uniform_int_distribution<size_t> kd(0, keys.size() - 1);
    std::uniform_int_distribution<size_t> ud(0, unknowns.size() - 1);

    auto ks = measure([&](size_t) {
        auto& k = keys[kd(rng)]; auto s = slot_fn(k);
        volatile bool v = pfa.verify(k, *s); (void)v;
    }, qi);
    rng.seed(456);
    auto us = measure([&](size_t) {
        auto& k = unknowns[ud(rng)];
        volatile bool v = pfa.verify(k, hasher.hash(k).value % keys.size()); (void)v;
    }, qi);

    size_t fps = 0;
    size_t fp_trials = std::min(unknowns.size(), size_t{1000000});
    for (size_t i = 0; i < fp_trials; ++i) {
        if (pfa.verify(unknowns[i], hasher.hash(unknowns[i]).value % keys.size())) ++fps;
    }

    return {"packed", FPBits, keys.size(), pfa.bits_per_key(keys.size()),
            duration_cast<microseconds>(t1 - t0).count() / 1000.0,
            ks.median_ns, ks.p99_ns, us.median_ns, us.p99_ns,
            static_cast<double>(fps) / fp_trials, pfa.memory_bytes()};
}

template<unsigned FPBits>
result run_xor(const std::vector<std::string>& keys,
               const std::vector<std::string>& unknowns, size_t qi) {
    xor_filter<FPBits> xf;
    auto t0 = high_resolution_clock::now();
    bool ok = xf.build(keys);
    auto t1 = high_resolution_clock::now();
    if (!ok) { std::cerr << "xor<" << FPBits << "> build failed\n"; return {}; }

    std::mt19937_64 rng{123};
    std::uniform_int_distribution<size_t> kd(0, keys.size() - 1);
    std::uniform_int_distribution<size_t> ud(0, unknowns.size() - 1);

    auto ks = measure([&](size_t) { volatile bool v = xf.verify(keys[kd(rng)]); (void)v; }, qi);
    rng.seed(456);
    auto us = measure([&](size_t) { volatile bool v = xf.verify(unknowns[ud(rng)]); (void)v; }, qi);

    size_t fps = 0;
    size_t fp_trials = std::min(unknowns.size(), size_t{1000000});
    for (size_t i = 0; i < fp_trials; ++i) { if (xf.verify(unknowns[i])) ++fps; }

    return {"xor", FPBits, keys.size(), xf.bits_per_key(keys.size()),
            duration_cast<microseconds>(t1 - t0).count() / 1000.0,
            ks.median_ns, ks.p99_ns, us.median_ns, us.p99_ns,
            static_cast<double>(fps) / fp_trials, xf.memory_bytes()};
}

template<unsigned FPBits>
result run_ribbon(const std::vector<std::string>& keys,
                  const std::vector<std::string>& unknowns, size_t qi) {
    ribbon_filter<FPBits> rf;
    auto t0 = high_resolution_clock::now();
    bool ok = rf.build(keys);
    auto t1 = high_resolution_clock::now();
    if (!ok) { std::cerr << "ribbon<" << FPBits << "> build failed\n"; return {}; }

    std::mt19937_64 rng{123};
    std::uniform_int_distribution<size_t> kd(0, keys.size() - 1);
    std::uniform_int_distribution<size_t> ud(0, unknowns.size() - 1);

    auto ks = measure([&](size_t) { volatile bool v = rf.verify(keys[kd(rng)]); (void)v; }, qi);
    rng.seed(456);
    auto us = measure([&](size_t) { volatile bool v = rf.verify(unknowns[ud(rng)]); (void)v; }, qi);

    size_t fps = 0;
    size_t fp_trials = std::min(unknowns.size(), size_t{1000000});
    for (size_t i = 0; i < fp_trials; ++i) { if (rf.verify(unknowns[i])) ++fps; }

    return {"ribbon", FPBits, keys.size(), rf.bits_per_key(keys.size()),
            duration_cast<microseconds>(t1 - t0).count() / 1000.0,
            ks.median_ns, ks.p99_ns, us.median_ns, us.p99_ns,
            static_cast<double>(fps) / fp_trials, rf.memory_bytes()};
}

template<unsigned FPBits>
result run_configurable(const std::vector<std::string>& keys,
                        const std::vector<std::string>& unknowns,
                        recsplit8& hasher, size_t qi) {
    auto slot_fn = [&](std::string_view k) -> std::optional<size_t> {
        auto s = hasher.slot_for(k);
        return s ? std::optional<size_t>{s->value} : std::nullopt;
    };
    fingerprint_verifier<FPBits> fv;
    auto t0 = high_resolution_clock::now();
    fv.build(keys, slot_fn, keys.size());
    auto t1 = high_resolution_clock::now();

    std::mt19937_64 rng{123};
    std::uniform_int_distribution<size_t> kd(0, keys.size() - 1);
    std::uniform_int_distribution<size_t> ud(0, unknowns.size() - 1);

    auto ks = measure([&](size_t) {
        auto& k = keys[kd(rng)]; auto s = slot_fn(k);
        volatile bool v = fv.verify(k, *s); (void)v;
    }, qi);
    rng.seed(456);
    auto us = measure([&](size_t) {
        auto& k = unknowns[ud(rng)];
        volatile bool v = fv.verify(k, hasher.hash(k).value % keys.size()); (void)v;
    }, qi);

    size_t fps = 0;
    size_t fp_trials = std::min(unknowns.size(), size_t{1000000});
    for (size_t i = 0; i < fp_trials; ++i) {
        if (fv.verify(unknowns[i], hasher.hash(unknowns[i]).value % keys.size())) ++fps;
    }

    return {"configurable", FPBits, keys.size(), fv.bits_per_key(keys.size()),
            duration_cast<microseconds>(t1 - t0).count() / 1000.0,
            ks.median_ns, ks.p99_ns, us.median_ns, us.p99_ns,
            static_cast<double>(fps) / fp_trials, fv.memory_bytes()};
}

int main(int argc, char** argv) {
    std::vector<size_t> key_counts = {1000000};
    size_t qi = 1000000;

    if (argc > 1) {
        key_counts.clear();
        for (int i = 1; i < argc; ++i) key_counts.push_back(std::stoul(argv[i]));
    }

    std::cerr << "Membership Verification Strategy Benchmark\n\n";
    print_header();

    for (size_t kc : key_counts) {
        auto keys = gen_keys(kc);
        auto unknowns = gen_unknowns(std::min(kc, size_t{1000000}));
        std::cerr << "=== " << keys.size() << " keys ===\n";

        std::cerr << "Building RecSplit..." << std::flush;
        auto hr = recsplit8::builder{}.add_all(keys).with_threads(4).build();
        if (!hr) { std::cerr << " FAILED, skipping\n"; continue; }
        auto hasher = std::move(hr.value());
        std::cerr << " done\n";

        // packed: 8, 16, 32
        print_result(run_packed<8>(keys, unknowns, hasher, qi));
        print_result(run_packed<16>(keys, unknowns, hasher, qi));
        print_result(run_packed<32>(keys, unknowns, hasher, qi));

        // xor: 8, 16, 32
        print_result(run_xor<8>(keys, unknowns, qi));
        print_result(run_xor<16>(keys, unknowns, qi));
        print_result(run_xor<32>(keys, unknowns, qi));

        // ribbon: 8, 16, 32
        print_result(run_ribbon<8>(keys, unknowns, qi));
        print_result(run_ribbon<16>(keys, unknowns, qi));
        print_result(run_ribbon<32>(keys, unknowns, qi));

        // configurable (wraps packed): 16 only (overhead test)
        print_result(run_configurable<16>(keys, unknowns, hasher, qi));
    }

    return 0;
}
```

- [ ] **Step 2: Add benchmark target to benchmarks/CMakeLists.txt**

After the `bench_perfect_hash_compare` block, add:

```cmake
# Membership verification strategy benchmark
add_executable(bench_membership bench_membership.cpp)
target_link_libraries(bench_membership PRIVATE benchmark_utils)
target_compile_options(bench_membership PRIVATE -O3 -march=native)
if(COMPILER_SUPPORTS_AVX2)
    target_compile_options(bench_membership PRIVATE -mavx2)
endif()
if(OpenMP_CXX_FOUND)
    target_link_libraries(bench_membership PRIVATE OpenMP::OpenMP_CXX)
endif()
```

- [ ] **Step 3: Build and smoke-test at 10K keys**

```bash
cd /home/spinoza/github/released/maph/build && cmake .. -DBUILD_TESTS=ON -DBUILD_BENCHMARKS=ON && make -j$(nproc) bench_membership 2>&1 | tail -5
./benchmarks/bench_membership 10000 2>&1
```

Expected: TSV table with 10 rows (3 packed + 3 xor + 3 ribbon + 1 configurable). All columns populated.

- [ ] **Step 4: Commit**

```bash
git add benchmarks/bench_membership.cpp benchmarks/CMakeLists.txt
git commit -m "feat: add membership verification benchmark

TSV output comparing packed/xor/ribbon/configurable across
fingerprint widths (8/16/32). Measures bits/key, build time,
query latency (known + unknown), and FP rate."
```

---

### Task 7: Run Full Benchmark Suite

**Files:** None (execution and validation)

- [ ] **Step 1: Run at 1M keys**

```bash
cd /home/spinoza/github/released/maph/build && ./benchmarks/bench_membership 1000000 2>&1
```

Verify:
- packed bits/key = 8.00, 16.00, 32.00
- xor bits/key ~ 9.8, 19.7, 39.4
- ribbon bits/key ~ 8-10, 16-19, 32-38
- FP rates in the right ballpark for each width
- configurable-16 numbers match packed-16 closely

- [ ] **Step 2: Run at 10M keys**

```bash
./benchmarks/bench_membership 10000000 2>&1
```

Note build times. If any strategy takes >5 minutes at 10M, note as a limitation.

- [ ] **Step 3: Run full test suite for final regression check**

```bash
ctest --output-on-failure 2>&1 | tail -20
```

Expected: All tests pass.

- [ ] **Step 4: Commit any fixes from benchmark runs**

If any fixes were needed:
```bash
git add -u && git commit -m "fix: address issues found during membership benchmark"
```
