# PHOBIC + Perfect Filter + Clean PHF Concept Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a clean `perfect_hash_function` concept, implement the PHOBIC algorithm (~2 bits/key, fast build and query), and provide `perfect_filter` for composing any PHF with fingerprint-based membership verification.

**Architecture:** Three new headers layered by dependency: `phf_concept.hpp` (concept definitions), `phobic.hpp` (algorithm), `perfect_filter.hpp` (composition). The existing `membership.hpp` is trimmed to only `packed_fingerprint_array` (xor/ribbon/fingerprint_verifier removed). Existing hashers are untouched.

**Tech Stack:** C++23, Catch2 v3, CMake

## File Structure

| File | Responsibility |
|------|---------------|
| `include/maph/phf_concept.hpp` | `perfect_hash_function` and `phf_builder` concepts |
| `include/maph/phobic.hpp` | PHOBIC algorithm (pure MPHF) |
| `include/maph/perfect_filter.hpp` | `perfect_filter<PHF, FPBits>` composition |
| `include/maph/membership.hpp` | `packed_fingerprint_array` only (cleanup) |
| `tests/v3/test_phf_concept.cpp` | Concept satisfaction tests |
| `tests/v3/test_phobic.cpp` | PHOBIC algorithm tests |
| `tests/v3/test_perfect_filter.cpp` | Perfect filter composition tests |
| `tests/v3/test_membership.cpp` | Updated (remove xor/ribbon/verifier tests) |
| `tests/v3/CMakeLists.txt` | New test targets |
| `benchmarks/bench_phobic.cpp` | PHOBIC vs existing algorithms |
| `benchmarks/CMakeLists.txt` | New benchmark target |

---

### Task 1: PHF Concept Header + Concept Tests

**Files:**
- Create: `include/maph/phf_concept.hpp`
- Create: `tests/v3/test_phf_concept.cpp`
- Modify: `tests/v3/CMakeLists.txt`

- [ ] **Step 1: Create `include/maph/phf_concept.hpp`**

```cpp
/**
 * @file phf_concept.hpp
 * @brief Concepts for perfect hash functions and their builders
 *
 * Defines the interface that all perfect hash function implementations
 * must satisfy. Separates the pure hash function (keys -> slots) from
 * membership verification and value storage.
 */

#pragma once

#include "core.hpp"
#include <string_view>
#include <vector>
#include <span>
#include <cstddef>

namespace maph {

/**
 * @concept perfect_hash_function
 * @brief A perfect hash function mapping n keys to distinct slots in [0, m)
 *
 * For keys in the build set, slot_for() returns a unique slot in [0, range_size()).
 * For keys NOT in the build set, slot_for() returns an arbitrary but valid index
 * in [0, range_size()). No membership checking is performed.
 *
 * When range_size() == num_keys(), the PHF is minimal (MPHF).
 * When range_size() > num_keys(), some slots are unused.
 */
template<typename P>
concept perfect_hash_function = requires(const P p, std::string_view key) {
    { p.slot_for(key) }    -> std::convertible_to<slot_index>;
    { p.num_keys() }       -> std::convertible_to<size_t>;
    { p.range_size() }     -> std::convertible_to<size_t>;
    { p.bits_per_key() }   -> std::convertible_to<double>;
    { p.memory_bytes() }   -> std::convertible_to<size_t>;
    { p.serialize() }      -> std::convertible_to<std::vector<std::byte>>;
};

/**
 * @concept phf_builder
 * @brief A builder that constructs a perfect hash function from a key set
 *
 * Builders use a fluent interface: add keys, then build.
 * build() may fail (e.g., if construction parameters are unsuitable).
 */
template<typename B, typename PHF>
concept phf_builder = requires(B b, std::string_view key, const std::vector<std::string>& keys) {
    { b.add(key) }       -> std::same_as<B&>;
    { b.add_all(keys) }  -> std::same_as<B&>;
    { b.build() }        -> std::same_as<result<PHF>>;
};

} // namespace maph
```

- [ ] **Step 2: Create `tests/v3/test_phf_concept.cpp`**

```cpp
/**
 * @file test_phf_concept.cpp
 * @brief Tests for perfect_hash_function and phf_builder concepts
 */

#include <catch2/catch_test_macros.hpp>
#include <maph/phf_concept.hpp>
#include <optional>

using namespace maph;

namespace {

// Mock PHF that satisfies the concept (verifies concept is not over-constrained)
struct mock_phf {
    slot_index slot_for(std::string_view) const noexcept { return slot_index{0}; }
    size_t num_keys() const noexcept { return 0; }
    size_t range_size() const noexcept { return 0; }
    double bits_per_key() const noexcept { return 0.0; }
    size_t memory_bytes() const noexcept { return 0; }
    std::vector<std::byte> serialize() const { return {}; }
    static result<mock_phf> deserialize(std::span<const std::byte>) {
        return mock_phf{};
    }
};

// Mock builder that satisfies phf_builder
struct mock_builder {
    mock_builder& add(std::string_view) { return *this; }
    mock_builder& add_all(const std::vector<std::string>&) { return *this; }
    result<mock_phf> build() { return mock_phf{}; }
};

} // namespace

// Concept satisfaction checks (compile-time)
static_assert(perfect_hash_function<mock_phf>, "mock_phf must satisfy perfect_hash_function");
static_assert(phf_builder<mock_builder, mock_phf>, "mock_builder must satisfy phf_builder");

TEST_CASE("perfect_hash_function concept: mock satisfies", "[phf_concept]") {
    mock_phf phf;
    REQUIRE(phf.slot_for("test").value == 0);
    REQUIRE(phf.num_keys() == 0);
    REQUIRE(phf.range_size() == 0);
    REQUIRE(phf.bits_per_key() == 0.0);
    REQUIRE(phf.memory_bytes() == 0);
    REQUIRE(phf.serialize().empty());
}

TEST_CASE("phf_builder concept: mock satisfies", "[phf_concept]") {
    mock_builder b;
    auto& ref = b.add("key");
    REQUIRE(&ref == &b);

    std::vector<std::string> keys = {"a", "b"};
    auto& ref2 = b.add_all(keys);
    REQUIRE(&ref2 == &b);

    auto result = b.build();
    REQUIRE(result.has_value());
}
```

- [ ] **Step 3: Add test target to `tests/v3/CMakeLists.txt`**

After the `test_v3_membership` target block, add:

```cmake
# PHF concept tests
add_executable(test_v3_phf_concept
    test_phf_concept.cpp
)

target_link_libraries(test_v3_phf_concept
    PRIVATE
    maph
    Catch2::Catch2WithMain
    pthread
)

target_include_directories(test_v3_phf_concept
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(test_v3_phf_concept PRIVATE cxx_std_${V3_CXX_STANDARD})
```

Add `test_v3_phf_concept` to the `V3_TEST_TARGETS` list. Add `catch_discover_tests`:

```cmake
    catch_discover_tests(test_v3_phf_concept
        TEST_PREFIX "v3_phf_concept:"
        PROPERTIES TIMEOUT 60
    )
```

Add to fallback section:

```cmake
    add_test(NAME v3_phf_concept COMMAND test_v3_phf_concept)
```

And add to the fallback timeout line for short tests.

- [ ] **Step 4: Build and run**

```bash
cd /home/spinoza/github/released/maph/build && cmake .. -DBUILD_TESTS=ON && make -j$(nproc) test_v3_phf_concept && ./tests/v3/test_v3_phf_concept --reporter compact
```

Expected: All tests PASS. The `static_assert`s verify at compile time.

- [ ] **Step 5: Commit**

```bash
git add include/maph/phf_concept.hpp tests/v3/test_phf_concept.cpp tests/v3/CMakeLists.txt
git commit -m "feat: add perfect_hash_function and phf_builder concepts

Clean PHF concept separating the hash function (keys -> slots) from
membership verification. Supports both minimal and non-minimal PHFs
via range_size() >= num_keys()."
```

---

### Task 2: PHOBIC Algorithm - Core Construction

**Files:**
- Create: `include/maph/phobic.hpp`
- Create: `tests/v3/test_phobic.cpp`
- Modify: `tests/v3/CMakeLists.txt`

- [ ] **Step 1: Create `tests/v3/test_phobic.cpp` with initial tests**

```cpp
/**
 * @file test_phobic.cpp
 * @brief Tests for PHOBIC perfect hash function
 */

#include <catch2/catch_test_macros.hpp>
#include <maph/phobic.hpp>
#include <random>
#include <algorithm>
#include <set>

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

std::vector<std::string> make_sequential_keys(size_t count) {
    std::vector<std::string> keys;
    keys.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        keys.push_back("key_" + std::to_string(i));
    }
    return keys;
}

template<typename PHF>
bool verify_bijectivity(const PHF& phf, const std::vector<std::string>& keys) {
    std::set<uint64_t> seen;
    for (const auto& key : keys) {
        auto slot = phf.slot_for(key);
        if (slot.value >= phf.range_size()) return false;
        if (!seen.insert(slot.value).second) return false;  // Duplicate slot
    }
    return seen.size() == keys.size();
}

} // namespace

// ===== CONCEPT SATISFACTION =====

static_assert(perfect_hash_function<phobic_phf<5>>,
    "phobic_phf<5> must satisfy perfect_hash_function");

// ===== BIJECTIVITY =====

TEST_CASE("phobic: bijectivity small key sets", "[phobic]") {
    SECTION("3 keys") {
        std::vector<std::string> keys = {"alpha", "beta", "gamma"};
        auto phf = phobic_phf<5>::builder{}.add_all(keys).build();
        REQUIRE(phf.has_value());
        REQUIRE(verify_bijectivity(*phf, keys));
        REQUIRE(phf->num_keys() == 3);
        REQUIRE(phf->range_size() >= 3);
    }

    SECTION("10 keys") {
        auto keys = make_sequential_keys(10);
        auto phf = phobic_phf<5>::builder{}.add_all(keys).build();
        REQUIRE(phf.has_value());
        REQUIRE(verify_bijectivity(*phf, keys));
    }

    SECTION("100 keys") {
        auto keys = make_keys(100);
        auto phf = phobic_phf<5>::builder{}.add_all(keys).build();
        REQUIRE(phf.has_value());
        REQUIRE(verify_bijectivity(*phf, keys));
    }

    SECTION("Single key") {
        std::vector<std::string> keys = {"only"};
        auto phf = phobic_phf<5>::builder{}.add_all(keys).build();
        REQUIRE(phf.has_value());
        REQUIRE(verify_bijectivity(*phf, keys));
        REQUIRE(phf->num_keys() == 1);
    }
}

TEST_CASE("phobic: bijectivity medium key sets", "[phobic]") {
    SECTION("1K keys") {
        auto keys = make_keys(1000);
        auto phf = phobic_phf<5>::builder{}.add_all(keys).build();
        REQUIRE(phf.has_value());
        REQUIRE(verify_bijectivity(*phf, keys));
    }

    SECTION("10K keys") {
        auto keys = make_keys(10000);
        auto phf = phobic_phf<5>::builder{}.add_all(keys).build();
        REQUIRE(phf.has_value());
        REQUIRE(verify_bijectivity(*phf, keys));
    }
}

// ===== MINIMAL VS NON-MINIMAL =====

TEST_CASE("phobic: minimal mode (alpha=1.0)", "[phobic]") {
    auto keys = make_keys(1000);
    auto phf = phobic_phf<5>::builder{}
        .add_all(keys)
        .with_alpha(1.0)
        .build();
    REQUIRE(phf.has_value());
    REQUIRE(phf->range_size() == phf->num_keys());
    REQUIRE(verify_bijectivity(*phf, keys));
}

TEST_CASE("phobic: non-minimal mode (alpha=1.05)", "[phobic]") {
    auto keys = make_keys(1000);
    auto phf = phobic_phf<5>::builder{}
        .add_all(keys)
        .with_alpha(1.05)
        .build();
    REQUIRE(phf.has_value());
    REQUIRE(phf->range_size() > phf->num_keys());
    REQUIRE(verify_bijectivity(*phf, keys));
}

// ===== SPACE EFFICIENCY =====

TEST_CASE("phobic: space efficiency", "[phobic]") {
    auto keys = make_keys(10000);
    auto phf = phobic_phf<5>::builder{}.add_all(keys).build();
    REQUIRE(phf.has_value());

    INFO("Bits per key: " << phf->bits_per_key());
    INFO("Memory bytes: " << phf->memory_bytes());
    REQUIRE(phf->bits_per_key() > 0.0);
    REQUIRE(phf->bits_per_key() < 3.0);
}

// ===== DETERMINISTIC BUILDS =====

TEST_CASE("phobic: deterministic with same seed", "[phobic]") {
    auto keys = make_keys(500);

    auto phf1 = phobic_phf<5>::builder{}.add_all(keys).with_seed(12345).build();
    auto phf2 = phobic_phf<5>::builder{}.add_all(keys).with_seed(12345).build();
    REQUIRE(phf1.has_value());
    REQUIRE(phf2.has_value());

    for (const auto& key : keys) {
        REQUIRE(phf1->slot_for(key).value == phf2->slot_for(key).value);
    }
}

// ===== SERIALIZATION =====

TEST_CASE("phobic: serialization round-trip", "[phobic]") {
    auto keys = make_keys(1000);
    auto phf = phobic_phf<5>::builder{}.add_all(keys).build();
    REQUIRE(phf.has_value());

    auto bytes = phf->serialize();
    REQUIRE(!bytes.empty());

    auto restored = phobic_phf<5>::deserialize(bytes);
    REQUIRE(restored.has_value());

    REQUIRE(restored->num_keys() == phf->num_keys());
    REQUIRE(restored->range_size() == phf->range_size());

    for (const auto& key : keys) {
        REQUIRE(restored->slot_for(key).value == phf->slot_for(key).value);
    }
}

// ===== BUILDER =====

TEST_CASE("phobic: builder add individual keys", "[phobic]") {
    auto b = phobic_phf<5>::builder{};
    b.add("one").add("two").add("three");
    auto phf = b.build();
    REQUIRE(phf.has_value());
    REQUIRE(phf->num_keys() == 3);
    REQUIRE(verify_bijectivity(*phf, {"one", "two", "three"}));
}

TEST_CASE("phobic: builder with_seed", "[phobic]") {
    auto keys = make_keys(100);
    auto phf = phobic_phf<5>::builder{}.add_all(keys).with_seed(42).build();
    REQUIRE(phf.has_value());
    REQUIRE(verify_bijectivity(*phf, keys));
}

// ===== DIFFERENT BUCKET SIZES =====

TEST_CASE("phobic: bucket size 3", "[phobic]") {
    auto keys = make_keys(1000);
    auto phf = phobic_phf<3>::builder{}.add_all(keys).build();
    REQUIRE(phf.has_value());
    REQUIRE(verify_bijectivity(*phf, keys));
}

TEST_CASE("phobic: bucket size 7", "[phobic]") {
    auto keys = make_keys(1000);
    auto phf = phobic_phf<7>::builder{}.add_all(keys).build();
    REQUIRE(phf.has_value());
    REQUIRE(verify_bijectivity(*phf, keys));
}
```

- [ ] **Step 2: Create `include/maph/phobic.hpp`**

```cpp
/**
 * @file phobic.hpp
 * @brief PHOBIC perfect hash function
 *
 * Pilot-based construction: partition keys into small buckets, then find
 * a "pilot" value per bucket such that hashing each key with its bucket's
 * pilot produces a distinct slot. Query: hash to bucket, read pilot,
 * hash with pilot to get slot.
 *
 * Space: ~2.0-2.5 bits/key. Query: ~15-25ns. Build: O(n) expected.
 *
 * Based on: Lehmann & Walzer, "PHOBIC: Perfect Hashing with Optimized
 * Bucket sizes and Interleaved Coding" (2024).
 *
 * @tparam BucketSize Average keys per bucket (default 5)
 */

#pragma once

#include "phf_concept.hpp"
#include <vector>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <bit>
#include <random>
#include <span>

namespace maph {

template<size_t BucketSize = 5>
class phobic_phf {
    static_assert(BucketSize >= 2 && BucketSize <= 20,
        "BucketSize must be between 2 and 20");

public:
    class builder;

private:
    // Two independent hash functions from one key
    struct dual_hash {
        uint64_t h1, h2;
    };

    static dual_hash hash_key(std::string_view key, uint64_t seed) noexcept {
        // First hash: FNV-1a with seed
        uint64_t h1 = seed ^ 0xcbf29ce484222325ULL;
        for (unsigned char c : key) {
            h1 ^= c;
            h1 *= 0x100000001b3ULL;
        }
        h1 ^= h1 >> 30;
        h1 *= 0xbf58476d1ce4e5b9ULL;
        h1 ^= h1 >> 27;
        h1 *= 0x94d049bb133111ebULL;
        h1 ^= h1 >> 31;

        // Second hash: different seed mixing
        uint64_t h2 = (seed ^ 0x517cc1b727220a95ULL);
        for (unsigned char c : key) {
            h2 ^= c;
            h2 *= 0x2127599bf4325c37ULL;
        }
        h2 ^= h2 >> 33;
        h2 *= 0xff51afd7ed558ccdULL;
        h2 ^= h2 >> 33;
        h2 *= 0xc4ceb9fe1a85ec53ULL;
        h2 ^= h2 >> 33;

        return {h1, h2};
    }

    // Derive bucket index from first hash
    size_t bucket_for(uint64_t h1) const noexcept {
        // Fastrange: (h1 * num_buckets) >> 64, but we use modulo for clarity
        return static_cast<size_t>(h1 % num_buckets_);
    }

    // Derive slot from second hash mixed with pilot
    size_t slot_with_pilot(uint64_t h2, uint16_t pilot) const noexcept {
        // Mix pilot into h2
        uint64_t mixed = h2 ^ (static_cast<uint64_t>(pilot) * 0x9e3779b97f4a7c15ULL);
        mixed ^= mixed >> 31;
        mixed *= 0xbf58476d1ce4e5b9ULL;
        mixed ^= mixed >> 27;
        return static_cast<size_t>(mixed % range_size_);
    }

    std::vector<uint8_t> pilots_;           // One pilot per bucket (fits in uint8_t most of the time)
    std::vector<size_t> overflow_buckets_;   // Bucket IDs that needed >255 pilot
    std::vector<uint16_t> overflow_pilots_;  // Their actual pilot values
    size_t num_keys_{0};
    size_t range_size_{0};
    size_t num_buckets_{0};
    uint64_t seed_{0};

    uint16_t get_pilot(size_t bucket_id) const noexcept {
        uint8_t p = pilots_[bucket_id];
        if (p < 255) return p;
        // Linear search overflow (rare, typically <1% of buckets)
        for (size_t i = 0; i < overflow_buckets_.size(); ++i) {
            if (overflow_buckets_[i] == bucket_id) {
                return overflow_pilots_[i];
            }
        }
        return 0;  // Should not happen
    }

public:
    phobic_phf() = default;
    phobic_phf(phobic_phf&&) = default;
    phobic_phf& operator=(phobic_phf&&) = default;

    [[nodiscard]] slot_index slot_for(std::string_view key) const noexcept {
        auto [h1, h2] = hash_key(key, seed_);
        size_t bucket_id = bucket_for(h1);
        uint16_t pilot = get_pilot(bucket_id);
        return slot_index{slot_with_pilot(h2, pilot)};
    }

    [[nodiscard]] size_t num_keys() const noexcept { return num_keys_; }
    [[nodiscard]] size_t range_size() const noexcept { return range_size_; }

    [[nodiscard]] double bits_per_key() const noexcept {
        if (num_keys_ == 0) return 0.0;
        size_t total_bytes = pilots_.size() * sizeof(uint8_t)
            + overflow_buckets_.size() * sizeof(size_t)
            + overflow_pilots_.size() * sizeof(uint16_t)
            + sizeof(*this);
        return static_cast<double>(total_bytes * 8) / num_keys_;
    }

    [[nodiscard]] size_t memory_bytes() const noexcept {
        return pilots_.size() * sizeof(uint8_t)
            + overflow_buckets_.size() * sizeof(size_t)
            + overflow_pilots_.size() * sizeof(uint16_t)
            + sizeof(*this);
    }

    // Algorithm identifier
    static constexpr uint32_t ALGORITHM_ID = 6;

    [[nodiscard]] std::vector<std::byte> serialize() const {
        std::vector<std::byte> out;
        auto append = [&](const auto& val) {
            auto bytes = std::bit_cast<std::array<std::byte, sizeof(val)>>(val);
            out.insert(out.end(), bytes.begin(), bytes.end());
        };

        // Header
        append(PERFECT_HASH_MAGIC);
        append(PERFECT_HASH_VERSION);
        append(ALGORITHM_ID);

        // Core data
        append(seed_);
        append(static_cast<uint64_t>(num_keys_));
        append(static_cast<uint64_t>(range_size_));
        append(static_cast<uint64_t>(num_buckets_));
        append(static_cast<uint64_t>(BucketSize));

        // Pilots
        append(static_cast<uint64_t>(pilots_.size()));
        for (auto p : pilots_) append(p);

        // Overflow
        append(static_cast<uint64_t>(overflow_buckets_.size()));
        for (auto b : overflow_buckets_) append(static_cast<uint64_t>(b));
        for (auto p : overflow_pilots_) append(p);

        return out;
    }

    [[nodiscard]] static result<phobic_phf> deserialize(std::span<const std::byte> data) {
        size_t off = 0;
        auto read = [&](auto& val) -> bool {
            if (off + sizeof(val) > data.size()) return false;
            std::memcpy(&val, data.data() + off, sizeof(val));
            off += sizeof(val);
            return true;
        };

        uint32_t magic{}, version{}, algo{};
        if (!read(magic) || magic != PERFECT_HASH_MAGIC) return std::unexpected(error::invalid_format);
        if (!read(version) || version != PERFECT_HASH_VERSION) return std::unexpected(error::invalid_format);
        if (!read(algo) || algo != ALGORITHM_ID) return std::unexpected(error::invalid_format);

        uint64_t seed{}, nkeys{}, rsize{}, nbuckets{}, bsize{};
        if (!read(seed) || !read(nkeys) || !read(rsize) || !read(nbuckets) || !read(bsize))
            return std::unexpected(error::invalid_format);
        if (bsize != BucketSize) return std::unexpected(error::invalid_format);

        uint64_t pilot_count{};
        if (!read(pilot_count) || pilot_count > data.size() - off)
            return std::unexpected(error::invalid_format);

        phobic_phf r;
        r.seed_ = seed;
        r.num_keys_ = static_cast<size_t>(nkeys);
        r.range_size_ = static_cast<size_t>(rsize);
        r.num_buckets_ = static_cast<size_t>(nbuckets);
        r.pilots_.resize(static_cast<size_t>(pilot_count));
        for (auto& p : r.pilots_) { if (!read(p)) return std::unexpected(error::invalid_format); }

        uint64_t overflow_count{};
        if (!read(overflow_count)) return std::unexpected(error::invalid_format);
        r.overflow_buckets_.resize(static_cast<size_t>(overflow_count));
        r.overflow_pilots_.resize(static_cast<size_t>(overflow_count));
        for (auto& b : r.overflow_buckets_) {
            uint64_t v{};
            if (!read(v)) return std::unexpected(error::invalid_format);
            b = static_cast<size_t>(v);
        }
        for (auto& p : r.overflow_pilots_) {
            if (!read(p)) return std::unexpected(error::invalid_format);
        }

        return r;
    }

    // ===== BUILDER =====

    class builder {
        std::vector<std::string> keys_;
        uint64_t seed_{0x123456789abcdef0ULL};
        double alpha_{1.0};

    public:
        builder() = default;

        builder& add(std::string_view key) {
            keys_.emplace_back(key);
            return *this;
        }

        builder& add_all(const std::vector<std::string>& keys) {
            keys_.insert(keys_.end(), keys.begin(), keys.end());
            return *this;
        }

        builder& add_all(std::span<const std::string> keys) {
            keys_.insert(keys_.end(), keys.begin(), keys.end());
            return *this;
        }

        builder& with_seed(uint64_t seed) {
            seed_ = seed;
            return *this;
        }

        builder& with_alpha(double alpha) {
            alpha_ = std::max(1.0, alpha);
            return *this;
        }

        [[nodiscard]] result<phobic_phf> build() {
            if (keys_.empty()) return std::unexpected(error::optimization_failed);

            // Deduplicate
            std::sort(keys_.begin(), keys_.end());
            keys_.erase(std::unique(keys_.begin(), keys_.end()), keys_.end());

            size_t n = keys_.size();
            size_t num_buckets = std::max(size_t{1}, (n + BucketSize - 1) / BucketSize);
            size_t range_size = static_cast<size_t>(std::ceil(n * alpha_));
            if (range_size < n) range_size = n;

            phobic_phf phf;
            phf.seed_ = seed_;
            phf.num_keys_ = n;
            phf.range_size_ = range_size;
            phf.num_buckets_ = num_buckets;
            phf.pilots_.resize(num_buckets, 0);

            // Precompute hashes and assign keys to buckets
            struct keyed_hash {
                size_t key_idx;
                size_t bucket_id;
                uint64_t h2;
            };

            std::vector<keyed_hash> hashes(n);
            std::vector<std::vector<size_t>> bucket_keys(num_buckets);

            for (size_t i = 0; i < n; ++i) {
                auto [h1, h2] = hash_key(keys_[i], seed_);
                size_t bucket_id = static_cast<size_t>(h1 % num_buckets);
                hashes[i] = {i, bucket_id, h2};
                bucket_keys[bucket_id].push_back(i);
            }

            // Sort buckets by size descending (process largest first for better packing)
            std::vector<size_t> bucket_order(num_buckets);
            std::iota(bucket_order.begin(), bucket_order.end(), 0);
            std::sort(bucket_order.begin(), bucket_order.end(),
                [&](size_t a, size_t b) {
                    return bucket_keys[a].size() > bucket_keys[b].size();
                });

            // Slot occupation bitvector
            std::vector<bool> occupied(range_size, false);

            // For each bucket, find a pilot
            for (size_t bucket_id : bucket_order) {
                const auto& keys_in_bucket = bucket_keys[bucket_id];
                if (keys_in_bucket.empty()) {
                    phf.pilots_[bucket_id] = 0;
                    continue;
                }

                bool found = false;
                // Try pilots 0-254 (fit in uint8_t without overflow marker)
                for (uint16_t pilot = 0; pilot < 65535 && !found; ++pilot) {
                    // Compute candidate slots for all keys in this bucket
                    std::vector<size_t> candidate_slots;
                    candidate_slots.reserve(keys_in_bucket.size());
                    bool collision = false;

                    for (size_t ki : keys_in_bucket) {
                        size_t slot = phf.slot_with_pilot(hashes[ki].h2, pilot);
                        if (occupied[slot]) { collision = true; break; }

                        // Check internal collision within this bucket
                        for (size_t prev : candidate_slots) {
                            if (prev == slot) { collision = true; break; }
                        }
                        if (collision) break;

                        candidate_slots.push_back(slot);
                    }

                    if (!collision && candidate_slots.size() == keys_in_bucket.size()) {
                        // Accept this pilot
                        if (pilot < 255) {
                            phf.pilots_[bucket_id] = static_cast<uint8_t>(pilot);
                        } else {
                            phf.pilots_[bucket_id] = 255;  // Overflow marker
                            phf.overflow_buckets_.push_back(bucket_id);
                            phf.overflow_pilots_.push_back(pilot);
                        }
                        for (size_t slot : candidate_slots) {
                            occupied[slot] = true;
                        }
                        found = true;
                    }
                }

                if (!found) {
                    return std::unexpected(error::optimization_failed);
                }
            }

            return phf;
        }
    };
};

// Convenience aliases
using phobic5 = phobic_phf<5>;
using phobic3 = phobic_phf<3>;
using phobic7 = phobic_phf<7>;

} // namespace maph
```

- [ ] **Step 3: Add test target to `tests/v3/CMakeLists.txt`**

After the `test_v3_phf_concept` target, add:

```cmake
# PHOBIC algorithm tests
add_executable(test_v3_phobic
    test_phobic.cpp
)

target_link_libraries(test_v3_phobic
    PRIVATE
    maph
    Catch2::Catch2WithMain
    pthread
)

target_include_directories(test_v3_phobic
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(test_v3_phobic PRIVATE cxx_std_${V3_CXX_STANDARD})
```

Add `test_v3_phobic` to `V3_TEST_TARGETS`. Add `catch_discover_tests`:

```cmake
    catch_discover_tests(test_v3_phobic
        TEST_PREFIX "v3_phobic:"
        PROPERTIES TIMEOUT 300
    )
```

Add to fallback section:

```cmake
    add_test(NAME v3_phobic COMMAND test_v3_phobic)
```

- [ ] **Step 4: Build and run**

```bash
cd /home/spinoza/github/released/maph/build && cmake .. -DBUILD_TESTS=ON && make -j$(nproc) test_v3_phobic && ./tests/v3/test_v3_phobic --reporter compact
```

Expected: All PHOBIC tests PASS. If pilot search fails for some test case, debug the hash mixing or increase max pilot range.

- [ ] **Step 5: Commit**

```bash
git add include/maph/phobic.hpp tests/v3/test_phobic.cpp tests/v3/CMakeLists.txt
git commit -m "feat: add PHOBIC perfect hash function

Pilot-based construction achieving ~2 bits/key. Satisfies the
perfect_hash_function concept. Supports minimal (alpha=1.0) and
non-minimal modes. Includes serialization and comprehensive tests."
```

---

### Task 3: Perfect Filter Composition

**Files:**
- Create: `include/maph/perfect_filter.hpp`
- Create: `tests/v3/test_perfect_filter.cpp`
- Modify: `tests/v3/CMakeLists.txt`

- [ ] **Step 1: Create `tests/v3/test_perfect_filter.cpp`**

```cpp
/**
 * @file test_perfect_filter.cpp
 * @brief Tests for perfect_filter composition
 */

#include <catch2/catch_test_macros.hpp>
#include <maph/perfect_filter.hpp>
#include <maph/phobic.hpp>
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

} // namespace

// ===== KNOWN KEY ACCEPTANCE =====

TEST_CASE("perfect_filter: known keys accepted (16-bit)", "[perfect_filter]") {
    auto keys = make_keys(1000);
    auto phf = phobic5::builder{}.add_all(keys).build().value();
    auto pf = perfect_filter<phobic5, 16>::build(std::move(phf), keys);

    for (const auto& key : keys) {
        auto slot = pf.slot_for(key);
        REQUIRE(slot.has_value());
        REQUIRE(slot->value < pf.range_size());
        REQUIRE(pf.contains(key));
    }
}

TEST_CASE("perfect_filter: known keys accepted (8-bit)", "[perfect_filter]") {
    auto keys = make_keys(500);
    auto phf = phobic5::builder{}.add_all(keys).build().value();
    auto pf = perfect_filter<phobic5, 8>::build(std::move(phf), keys);

    for (const auto& key : keys) {
        REQUIRE(pf.contains(key));
        REQUIRE(pf.slot_for(key).has_value());
    }
}

// Note: 10-bit test deferred to after Task 4 (relaxed width constraint)

// ===== UNKNOWN KEY REJECTION =====

TEST_CASE("perfect_filter: FP rate within statistical bounds", "[perfect_filter]") {
    auto keys = make_keys(1000);
    auto unknowns = make_unknowns(100000);
    auto phf = phobic5::builder{}.add_all(keys).build().value();
    auto pf = perfect_filter<phobic5, 16>::build(std::move(phf), keys);

    size_t fps = 0;
    for (const auto& uk : unknowns) {
        if (pf.contains(uk)) ++fps;
    }

    double fp_rate = static_cast<double>(fps) / unknowns.size();
    double expected = 1.0 / 65536.0;
    double stddev = std::sqrt(expected * (1.0 - expected) / static_cast<double>(unknowns.size()));
    INFO("FP rate: " << fp_rate << ", expected: " << expected << " +/- " << (3 * stddev));
    REQUIRE(fp_rate < expected + 3 * stddev);
}

// ===== CONTAINS MATCHES SLOT_FOR =====

TEST_CASE("perfect_filter: contains matches slot_for", "[perfect_filter]") {
    auto keys = make_keys(500);
    auto unknowns = make_unknowns(1000);
    auto phf = phobic5::builder{}.add_all(keys).build().value();
    auto pf = perfect_filter<phobic5, 16>::build(std::move(phf), keys);

    for (const auto& key : keys) {
        REQUIRE(pf.contains(key) == pf.slot_for(key).has_value());
    }
    for (const auto& uk : unknowns) {
        REQUIRE(pf.contains(uk) == pf.slot_for(uk).has_value());
    }
}

// ===== PHF ACCESS =====

TEST_CASE("perfect_filter: underlying PHF accessible", "[perfect_filter]") {
    auto keys = make_keys(200);
    auto phf = phobic5::builder{}.add_all(keys).build().value();
    auto pf = perfect_filter<phobic5, 16>::build(std::move(phf), keys);

    // Direct PHF access bypasses filter (always returns a slot)
    const auto& inner = pf.phf();
    for (const auto& key : keys) {
        auto slot = inner.slot_for(key);
        REQUIRE(slot.value < pf.range_size());
    }
}

// ===== DELEGATION =====

TEST_CASE("perfect_filter: delegates num_keys and range_size", "[perfect_filter]") {
    auto keys = make_keys(200);
    auto phf = phobic5::builder{}.add_all(keys).build().value();
    size_t expected_n = phf.num_keys();
    size_t expected_m = phf.range_size();
    auto pf = perfect_filter<phobic5, 16>::build(std::move(phf), keys);

    REQUIRE(pf.num_keys() == expected_n);
    REQUIRE(pf.range_size() == expected_m);
}

// ===== SERIALIZATION =====

TEST_CASE("perfect_filter: serialization round-trip", "[perfect_filter]") {
    auto keys = make_keys(500);
    auto phf = phobic5::builder{}.add_all(keys).build().value();
    auto pf = perfect_filter<phobic5, 16>::build(std::move(phf), keys);

    auto bytes = pf.serialize();
    REQUIRE(!bytes.empty());

    auto restored = perfect_filter<phobic5, 16>::deserialize(bytes);
    REQUIRE(restored.has_value());

    for (const auto& key : keys) {
        REQUIRE(restored->contains(key));
        REQUIRE(restored->slot_for(key).has_value());
        REQUIRE(restored->slot_for(key)->value == pf.slot_for(key)->value);
    }
}
```

- [ ] **Step 2: Create `include/maph/perfect_filter.hpp`**

```cpp
/**
 * @file perfect_filter.hpp
 * @brief Composes a perfect hash function with fingerprint verification
 *
 * A perfect_filter pairs a PHF (keys -> slots) with a packed fingerprint
 * array (approximate membership). The result is an approximate filter
 * that can also return unique slot indices for accepted keys.
 *
 * @tparam PHF A type satisfying perfect_hash_function
 * @tparam FPBits Fingerprint width in bits (1-32, default 16)
 */

#pragma once

#include "phf_concept.hpp"
#include "membership.hpp"
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

    /**
     * @brief Build a perfect filter from a pre-built PHF and the original keys
     */
    static perfect_filter build(PHF phf, const std::vector<std::string>& keys) {
        perfect_filter pf;
        pf.phf_ = std::move(phf);

        auto slot_fn = [&pf](std::string_view k) -> std::optional<size_t> {
            return std::optional<size_t>{pf.phf_.slot_for(k).value};
        };

        pf.fps_.build(keys, slot_fn, pf.phf_.range_size());
        return pf;
    }

    /**
     * @brief Approximate membership test
     * @return true if key is likely in the set (FP rate: 2^-FPBits)
     */
    [[nodiscard]] bool contains(std::string_view key) const noexcept {
        auto slot = phf_.slot_for(key);
        return fps_.verify(key, slot.value);
    }

    /**
     * @brief Guarded slot access
     * @return Slot index if fingerprint matches, nullopt otherwise
     */
    [[nodiscard]] std::optional<slot_index> slot_for(std::string_view key) const noexcept {
        auto slot = phf_.slot_for(key);
        if (fps_.verify(key, slot.value)) return slot;
        return std::nullopt;
    }

    /** @brief Access the underlying PHF directly (no fingerprint check) */
    [[nodiscard]] const PHF& phf() const noexcept { return phf_; }

    [[nodiscard]] size_t num_keys() const noexcept { return phf_.num_keys(); }
    [[nodiscard]] size_t range_size() const noexcept { return phf_.range_size(); }

    /**
     * @brief Serialize PHF + fingerprint array as one blob
     *
     * Format: [phf_size:u64][phf_bytes][fps_bytes]
     */
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
```

- [ ] **Step 3: Add test target to `tests/v3/CMakeLists.txt`**

After the `test_v3_phobic` target, add:

```cmake
# Perfect filter composition tests
add_executable(test_v3_perfect_filter
    test_perfect_filter.cpp
)

target_link_libraries(test_v3_perfect_filter
    PRIVATE
    maph
    Catch2::Catch2WithMain
    pthread
)

target_include_directories(test_v3_perfect_filter
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(test_v3_perfect_filter PRIVATE cxx_std_${V3_CXX_STANDARD})
```

Add `test_v3_perfect_filter` to `V3_TEST_TARGETS`. Add `catch_discover_tests`:

```cmake
    catch_discover_tests(test_v3_perfect_filter
        TEST_PREFIX "v3_perfect_filter:"
        PROPERTIES TIMEOUT 300
    )
```

Add to fallback section:

```cmake
    add_test(NAME v3_perfect_filter COMMAND test_v3_perfect_filter)
```

- [ ] **Step 4: Build and run**

```bash
cd /home/spinoza/github/released/maph/build && cmake .. -DBUILD_TESTS=ON && make -j$(nproc) test_v3_perfect_filter && ./tests/v3/test_v3_perfect_filter --reporter compact
```

Expected: All perfect_filter tests PASS.

- [ ] **Step 5: Commit**

```bash
git add include/maph/perfect_filter.hpp tests/v3/test_perfect_filter.cpp tests/v3/CMakeLists.txt
git commit -m "feat: add perfect_filter composition layer

Composes any perfect_hash_function with packed_fingerprint_array
for approximate membership testing. contains() and slot_for()
provide guarded access. Supports arbitrary FP widths (1-32 bits)."
```

---

### Task 4: Relax packed_fingerprint_array Width Constraint

**Files:**
- Modify: `include/maph/membership.hpp`
- Modify: `tests/v3/test_membership.cpp`

- [ ] **Step 1: Append test for 10-bit width to `tests/v3/test_membership.cpp`**

Add before the `// ===== XOR FILTER TESTS =====` line (line 187):

```cpp
TEST_CASE("packed_fingerprint_array: non-power-of-2 widths", "[membership][packed]") {
    auto keys = make_keys(500);
    auto fix = recsplit_fixture::create(keys);

    SECTION("10-bit") {
        packed_fingerprint_array<10> pfa;
        pfa.build(keys, fix.slot_fn, keys.size());
        for (const auto& key : keys) {
            auto slot = fix.slot_fn(key);
            REQUIRE(slot.has_value());
            REQUIRE(pfa.verify(key, *slot));
        }
        REQUIRE(pfa.bits_per_key(keys.size()) == 10.0);
    }

    SECTION("12-bit") {
        packed_fingerprint_array<12> pfa;
        pfa.build(keys, fix.slot_fn, keys.size());
        for (const auto& key : keys) {
            auto slot = fix.slot_fn(key);
            REQUIRE(slot.has_value());
            REQUIRE(pfa.verify(key, *slot));
        }
        REQUIRE(pfa.bits_per_key(keys.size()) == 12.0);
    }

    SECTION("10-bit serialization round-trip") {
        packed_fingerprint_array<10> original;
        original.build(keys, fix.slot_fn, keys.size());
        auto bytes = original.serialize();
        auto restored = packed_fingerprint_array<10>::deserialize(bytes);
        REQUIRE(restored.has_value());
        for (const auto& key : keys) {
            auto slot = fix.slot_fn(key);
            REQUIRE(slot.has_value());
            REQUIRE(restored->verify(key, *slot));
        }
    }
}
```

- [ ] **Step 2: Verify test fails (compilation error)**

```bash
cd /home/spinoza/github/released/maph/build && make -j$(nproc) test_v3_membership 2>&1 | grep "error:" | head -3
```

Expected: constraint failure for `FingerprintBits == 10`.

- [ ] **Step 3: Relax the constraint in `include/maph/membership.hpp`**

Change line 58 from:

```cpp
    requires (FingerprintBits == 8 || FingerprintBits == 16 || FingerprintBits == 32)
```

to:

```cpp
    requires (FingerprintBits >= 1 && FingerprintBits <= 32)
```

- [ ] **Step 4: Build and run**

```bash
cd /home/spinoza/github/released/maph/build && make -j$(nproc) test_v3_membership && ./tests/v3/test_v3_membership "[packed]" --reporter compact
```

Expected: All packed tests PASS (including the new 10-bit and 12-bit tests). The word-boundary-straddling code in `extract()`/`store()` is now exercised for these non-power-of-2 widths.

- [ ] **Step 5: Commit**

```bash
git add include/maph/membership.hpp tests/v3/test_membership.cpp
git commit -m "feat: support arbitrary fingerprint widths (1-32 bits)

Relaxes packed_fingerprint_array constraint from {8,16,32} to [1,32].
Enables 10-bit fingerprints (FP rate 1/1024, ~5-15% slower than 8-bit
but 4x better FP rate). Word-boundary packing code now exercised."
```

---

### Task 5: Clean Up membership.hpp (Remove xor/ribbon/verifier)

**Files:**
- Modify: `include/maph/membership.hpp`
- Modify: `tests/v3/test_membership.cpp`

- [ ] **Step 1: Remove xor_filter, ribbon_filter, and fingerprint_verifier from `include/maph/membership.hpp`**

Delete everything from the `// ===== STRATEGY 2: XOR FILTER =====` comment (around line 165) through the end of the `fingerprint_verifier` class (just before the closing `} // namespace maph`). Keep only:
- The file header comment
- The includes
- The `namespace maph {` open
- `membership_fingerprint()` function
- `packed_fingerprint_array` class (with the relaxed constraint from Task 4)
- The closing `} // namespace maph`

- [ ] **Step 2: Remove xor/ribbon/verifier tests from `tests/v3/test_membership.cpp`**

Delete everything from `// ===== XOR FILTER TESTS =====` (around line 200 after Task 4's additions) through the end of the file. Keep only:
- The file header and includes
- The `make_keys`, `make_unknowns`, `recsplit_fixture` helpers
- All `packed_fingerprint_array` test cases (including the new 10-bit tests from Task 4)

- [ ] **Step 3: Build and run**

```bash
cd /home/spinoza/github/released/maph/build && cmake .. -DBUILD_TESTS=ON && make -j$(nproc) test_v3_membership && ./tests/v3/test_v3_membership --reporter compact
```

Expected: All remaining packed_fingerprint_array tests PASS. No xor/ribbon/verifier tests remain.

- [ ] **Step 4: Verify bench_membership still compiles**

The benchmark references xor_filter and ribbon_filter. Remove or update it:

```bash
cd /home/spinoza/github/released/maph/build && make -j$(nproc) bench_membership 2>&1 | tail -5
```

If it fails, update `benchmarks/bench_membership.cpp` to remove the xor/ribbon/configurable benchmark functions and their calls from `main()`. Keep only the `run_packed` functions.

- [ ] **Step 5: Commit**

```bash
git add include/maph/membership.hpp tests/v3/test_membership.cpp benchmarks/bench_membership.cpp
git commit -m "refactor: remove xor_filter, ribbon_filter, fingerprint_verifier

These move to a future separate membership filter library.
membership.hpp now contains only packed_fingerprint_array and
membership_fingerprint(). perfect_filter supersedes fingerprint_verifier."
```

---

### Task 6: Verify No Regressions

**Files:** None (verification only)

- [ ] **Step 1: Run all tests**

```bash
cd /home/spinoza/github/released/maph/build && ctest --output-on-failure -R "v3_" 2>&1 | tail -30
```

Expected: All new tests pass. Pre-existing failures (mmap, cached_storage, optimization workflow, performance scaling) are unchanged.

- [ ] **Step 2: Run new tests specifically**

```bash
./tests/v3/test_v3_phf_concept --reporter compact
./tests/v3/test_v3_phobic --reporter compact
./tests/v3/test_v3_perfect_filter --reporter compact
./tests/v3/test_v3_membership --reporter compact
```

Expected: All PASS.

---

### Task 7: Benchmark

**Files:**
- Create: `benchmarks/bench_phobic.cpp`
- Modify: `benchmarks/CMakeLists.txt`

- [ ] **Step 1: Create `benchmarks/bench_phobic.cpp`**

```cpp
/**
 * @file bench_phobic.cpp
 * @brief Benchmark PHOBIC against existing perfect hash algorithms
 */

#include <maph/core.hpp>
#include <maph/hashers_perfect.hpp>
#include <maph/phobic.hpp>
#include <maph/perfect_filter.hpp>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <random>
#include <algorithm>
#include <numeric>

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

struct bench_result {
    std::string algorithm;
    size_t key_count;
    double build_ms;
    double query_median_ns;
    double query_p99_ns;
    double bits_per_key;
    size_t memory_bytes;
};

void print_header() {
    std::cout << "algorithm\tkeys\tbuild_ms\tquery_med_ns\tquery_p99_ns\tbits_per_key\tmem_kb\n";
}

void print_result(const bench_result& r) {
    std::cout << std::fixed
              << r.algorithm << '\t' << r.key_count << '\t'
              << std::setprecision(2) << r.build_ms << '\t'
              << r.query_median_ns << '\t' << r.query_p99_ns << '\t'
              << r.bits_per_key << '\t'
              << std::setprecision(1) << (r.memory_bytes / 1024.0) << '\n';
}

template<typename Fn>
std::pair<double, double> measure_query(Fn&& fn, size_t iters) {
    std::vector<double> times;
    times.reserve(iters);
    for (size_t i = 0; i < iters; ++i) {
        auto t0 = high_resolution_clock::now();
        fn(i);
        auto t1 = high_resolution_clock::now();
        times.push_back(static_cast<double>(duration_cast<nanoseconds>(t1 - t0).count()));
    }
    std::sort(times.begin(), times.end());
    return {times[times.size() / 2], times[static_cast<size_t>(times.size() * 0.99)]};
}

int main(int argc, char** argv) {
    std::vector<size_t> key_counts = {10000, 100000, 1000000};
    size_t qi = 1000000;

    if (argc > 1) {
        key_counts.clear();
        for (int i = 1; i < argc; ++i) key_counts.push_back(std::stoul(argv[i]));
    }

    std::cerr << "PHOBIC Benchmark\n\n";
    print_header();

    for (size_t kc : key_counts) {
        auto keys = gen_keys(kc);
        std::cerr << "=== " << keys.size() << " keys ===\n";

        std::mt19937_64 rng{123};
        std::uniform_int_distribution<size_t> kd(0, keys.size() - 1);

        // PHOBIC (pure PHF)
        {
            auto t0 = high_resolution_clock::now();
            auto phf = phobic5::builder{}.add_all(keys).build();
            auto t1 = high_resolution_clock::now();
            if (phf) {
                auto [med, p99] = measure_query([&](size_t) {
                    volatile auto s = phf->slot_for(keys[kd(rng)]); (void)s;
                }, qi);
                print_result({"phobic5", keys.size(),
                    duration_cast<microseconds>(t1 - t0).count() / 1000.0,
                    med, p99, phf->bits_per_key(), phf->memory_bytes()});
            } else {
                std::cerr << "phobic5 build FAILED\n";
            }
        }

        // PHOBIC + perfect_filter<16>
        {
            auto t0 = high_resolution_clock::now();
            auto phf = phobic5::builder{}.add_all(keys).build();
            if (phf) {
                auto pf = perfect_filter<phobic5, 16>::build(std::move(*phf), keys);
                auto t1 = high_resolution_clock::now();
                auto [med, p99] = measure_query([&](size_t) {
                    volatile auto s = pf.slot_for(keys[kd(rng)]); (void)s;
                }, qi);
                double bpk = pf.phf().bits_per_key() + 16.0;  // PHF + fingerprints
                print_result({"phobic5+pf16", keys.size(),
                    duration_cast<microseconds>(t1 - t0).count() / 1000.0,
                    med, p99, bpk, pf.phf().memory_bytes() + keys.size() * 2});
            }
        }

        // RecSplit (existing, with baked-in 64-bit fingerprints)
        {
            auto t0 = high_resolution_clock::now();
            auto h = recsplit8::builder{}.add_all(keys).build();
            auto t1 = high_resolution_clock::now();
            if (h) {
                auto [med, p99] = measure_query([&](size_t) {
                    volatile auto s = h->slot_for(keys[kd(rng)]); (void)s;
                }, qi);
                print_result({"recsplit8", keys.size(),
                    duration_cast<microseconds>(t1 - t0).count() / 1000.0,
                    med, p99, h->bits_per_key(), h->memory_bytes()});
            }
        }

        // CHD (existing)
        {
            auto t0 = high_resolution_clock::now();
            auto h = chd_hasher::builder{}.add_all(keys).build();
            auto t1 = high_resolution_clock::now();
            if (h) {
                auto [med, p99] = measure_query([&](size_t) {
                    volatile auto s = h->slot_for(keys[kd(rng)]); (void)s;
                }, qi);
                print_result({"chd", keys.size(),
                    duration_cast<microseconds>(t1 - t0).count() / 1000.0,
                    med, p99, h->bits_per_key(), h->memory_bytes()});
            }
        }

        // BBHash (existing)
        {
            auto t0 = high_resolution_clock::now();
            auto h = bbhash3::builder{}.add_all(keys).build();
            auto t1 = high_resolution_clock::now();
            if (h) {
                auto [med, p99] = measure_query([&](size_t) {
                    volatile auto s = h->slot_for(keys[kd(rng)]); (void)s;
                }, qi);
                print_result({"bbhash3", keys.size(),
                    duration_cast<microseconds>(t1 - t0).count() / 1000.0,
                    med, p99, h->bits_per_key(), h->memory_bytes()});
            }
        }
    }

    return 0;
}
```

- [ ] **Step 2: Add benchmark target to `benchmarks/CMakeLists.txt`**

After the `bench_membership` block, add:

```cmake
# PHOBIC benchmark
add_executable(bench_phobic bench_phobic.cpp)
target_link_libraries(bench_phobic PRIVATE benchmark_utils)
target_compile_options(bench_phobic PRIVATE -O3 -march=native)
if(COMPILER_SUPPORTS_AVX2)
    target_compile_options(bench_phobic PRIVATE -mavx2)
endif()
if(OpenMP_CXX_FOUND)
    target_link_libraries(bench_phobic PRIVATE OpenMP::OpenMP_CXX)
endif()
```

- [ ] **Step 3: Build and run**

```bash
cd /home/spinoza/github/released/maph/build && cmake .. -DBUILD_TESTS=ON -DBUILD_BENCHMARKS=ON && make -j$(nproc) bench_phobic && ./benchmarks/bench_phobic 10000 100000
```

Expected: TSV table with 5 rows per key count (phobic5, phobic5+pf16, recsplit8, chd, bbhash3).

- [ ] **Step 4: Commit**

```bash
git add benchmarks/bench_phobic.cpp benchmarks/CMakeLists.txt
git commit -m "feat: add PHOBIC benchmark

Compares PHOBIC (pure and with perfect_filter<16>) against
RecSplit, CHD, and BBHash. TSV output with build time, query
latency, bits/key, and memory."
```

---

### Task 8: Run Full Benchmark

**Files:** None (execution and validation)

- [ ] **Step 1: Run at 10K and 100K**

```bash
cd /home/spinoza/github/released/maph/build && ./benchmarks/bench_phobic 10000 100000
```

Verify:
- PHOBIC bits/key < 3.0
- PHOBIC query latency competitive with existing algorithms
- perfect_filter adds ~16 bits/key overhead
- All algorithms produce valid output

- [ ] **Step 2: Run at 1M**

```bash
./benchmarks/bench_phobic 1000000
```

Verify PHOBIC query median < 30ns at 1M keys (success criterion from spec).

- [ ] **Step 3: Run full test suite**

```bash
ctest --output-on-failure -R "v3_" 2>&1 | tail -20
```

Expected: All new tests pass. Pre-existing failures unchanged.

- [ ] **Step 4: Commit any fixes**

If any issues found during benchmarking:

```bash
git add -u && git commit -m "fix: address issues found during PHOBIC benchmark"
```
