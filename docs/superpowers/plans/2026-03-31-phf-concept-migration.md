# PHF Concept Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate all five existing perfect hash algorithms to the clean `perfect_hash_function` concept, stripping baked-in fingerprints and overflow handling, and updating the entire layer stack (core, hashers, table, optimization, maph) to match.

**Architecture:** Each algorithm is rewritten to return `slot_index` (non-optional) from `slot_for()`, with no fingerprint storage. Builds that can't place all keys retry with adjusted parameters rather than using overflow. The old `hasher`/`perfect_hasher` concepts are removed and replaced by `perfect_hash_function` from `phf_concept.hpp`. Upper layers (`table.hpp`, `hashers.hpp`) switch to unconstrained templates.

**Tech Stack:** C++23, Catch2 v3, CMake

## File Structure

| File | Action | Description |
|------|--------|-------------|
| `include/maph/core.hpp` | Modify | Remove `hasher`, `perfect_hasher` concepts |
| `include/maph/hashers.hpp` | Modify | Unconstrain templates, update minimal_perfect_hasher |
| `include/maph/table.hpp` | Modify | Unconstrain template parameter |
| `include/maph/optimization.hpp` | Modify | Update for new minimal_perfect_hasher interface |
| `include/maph/maph.hpp` | Modify | Update for new interfaces |
| `include/maph/hashers_perfect.hpp` | Rewrite | All 5 algorithms: strip fingerprints, satisfy `perfect_hash_function` |
| `tests/v3/test_core.cpp` | Modify | Remove old concept tests |
| `tests/v3/test_hashers.cpp` | Modify | Update for new interfaces |
| `tests/v3/test_perfect_hash.cpp` | Rewrite | New PHF-style tests for all 5 algorithms |
| `tests/v3/test_perfect_hash_extended.cpp` | Rewrite | Update for new interface |
| `tests/v3/test_table.cpp` | Modify | Update if affected |
| `tests/v3/test_integration.cpp` | Modify | Update if affected |
| `tests/v3/test_properties.cpp` | Modify | Update if affected |
| `benchmarks/bench_phobic.cpp` | Modify | Add all algorithms for fair comparison |
| `benchmarks/bench_perfect_hash_compare.cpp` | Modify | Update for new interface |

---

### Task 1: Update core.hpp -- Remove Old Concepts

**Files:**
- Modify: `include/maph/core.hpp`
- Modify: `tests/v3/test_core.cpp`

- [ ] **Step 1: Remove old concepts from core.hpp**

In `include/maph/core.hpp`, delete the `hasher` concept (lines 68-76), the `perfect_hasher` concept (lines 92-100), and their doc comments. Keep `storage_backend` concept, strong types, error handling, key/value types, and slot template.

The section from `// ===== CORE CONCEPTS =====` should contain only `storage_backend` after this change:

```cpp
// ===== CORE CONCEPTS =====
// Define clear interfaces using concepts

/**
 * @concept storage_backend
 * @brief A type that provides storage for slots
 */
template<typename S>
concept storage_backend = requires(S s, slot_index idx, hash_value hash, std::span<const std::byte> data) {
    { s.read(idx) };
    { s.write(idx, hash, data) };
    { s.clear(idx) };
    { s.get_slot_count() };
    { s.empty(idx) };
    { s.hash_at(idx) };
};
```

- [ ] **Step 2: Update test_core.cpp**

In `tests/v3/test_core.cpp`, find the test case that checks `hasher` and `perfect_hasher` concepts (search for `"Concept compliance"` or `mock_hasher` or `mock_perfect_hasher`). Remove the `mock_hasher` and `mock_perfect_hasher` structs and the concept compliance test sections for `hasher` and `perfect_hasher`. Keep any `storage_backend` concept tests.

- [ ] **Step 3: Build and run core tests**

```bash
cd /home/spinoza/github/released/maph/build && cmake .. -DBUILD_TESTS=ON && make -j$(nproc) test_v3_core 2>&1 | tail -10
./tests/v3/test_v3_core --reporter compact
```

Note: This will cause compilation failures in `hashers.hpp` and `table.hpp` because they reference the removed concepts. That's expected. Only `test_v3_core` needs to pass at this step (it may need `hashers.hpp` fix first). If so, do Step 4 before testing.

- [ ] **Step 4: Fix hashers.hpp template constraints**

In `include/maph/hashers.hpp`:

Change line 60 from:
```cpp
template<hasher H>
```
to:
```cpp
template<typename H>
```

Change line 167 from:
```cpp
template<perfect_hasher P, hasher H>
```
to:
```cpp
template<typename P, typename H>
```

Change line 204 from:
```cpp
template<perfect_hasher P, hasher H>
```
to:
```cpp
template<typename P, typename H>
```

- [ ] **Step 5: Fix table.hpp template constraint**

In `include/maph/table.hpp`, change line 24 from:
```cpp
template<hasher Hasher, typename Storage>
```
to:
```cpp
template<typename Hasher, typename Storage>
```

- [ ] **Step 6: Build all test targets to verify compilation**

```bash
cd /home/spinoza/github/released/maph/build && make -j$(nproc) test_v3_core test_v3_hashers test_v3_table 2>&1 | tail -10
./tests/v3/test_v3_core --reporter compact
```

Expected: Core tests pass. Hashers and table may have other issues from the algorithm changes needed in later tasks; that's OK for now.

- [ ] **Step 7: Commit**

```bash
git add include/maph/core.hpp include/maph/hashers.hpp include/maph/table.hpp tests/v3/test_core.cpp
git commit -m "refactor: remove hasher and perfect_hasher concepts

Replaced by perfect_hash_function from phf_concept.hpp.
Template parameters in hashers.hpp and table.hpp are now
unconstrained. storage_backend concept retained."
```

---

### Task 2: Rewrite hashers_perfect.hpp -- All Five Algorithms

**Files:**
- Modify: `include/maph/hashers_perfect.hpp`

This is the largest task. All five algorithms (RecSplit, CHD, BBHash, FCH, PTHash) are rewritten to satisfy `perfect_hash_function`. The transformation is identical for each:

- [ ] **Step 1: Add phf_concept.hpp include**

At the top of `include/maph/hashers_perfect.hpp`, add:
```cpp
#include "phf_concept.hpp"
```

- [ ] **Step 2: Remove shared fingerprint infrastructure**

Delete from `hashers_perfect.hpp`:
- `struct perfect_hash_stats` (replace with per-algorithm `bits_per_key()`/`memory_bytes()`)
- `concept perfect_hash_builder` (replaced by `phf_builder` in `phf_concept.hpp`)
- `find_fingerprint_simd()` function (both AVX2 and scalar versions)
- `fingerprint64()` function
- The `static_assert` for endianness can stay (still needed for serialization)
- `MAX_SERIALIZED_ELEMENT_COUNT` stays
- `PERFECT_HASH_MAGIC` and `PERFECT_HASH_VERSION` stay

- [ ] **Step 3: Rewrite RecSplit (`recsplit_hasher`)**

Apply these changes to `recsplit_hasher`:

**Remove members:**
- `std::vector<uint64_t> fingerprints_`
- `std::vector<uint64_t> overflow_fingerprints_`
- `std::vector<size_t> overflow_slots_`
- `size_t perfect_count_`

**Remove methods:**
- `hash()` (the old `hasher` concept method)
- `is_perfect_for()`
- `max_slots()` (replace with `range_size()`)
- `perfect_count()`
- `overflow_count()`

**Change `slot_for()`:**
Return `slot_index` (not `optional<slot_index>`). Remove fingerprint verification and overflow fallback. The logic becomes:
```cpp
[[nodiscard]] slot_index slot_for(std::string_view key) const noexcept {
    if (key_count_ == 0) return slot_index{0};
    size_t bucket_idx = bucket_for_key(key);
    if (buckets_[bucket_idx].num_keys == 0) return slot_index{0};
    size_t local_slot = slot_in_bucket(key, bucket_idx);
    size_t global_slot = bucket_offsets_[bucket_idx] + local_slot;
    return slot_index{global_slot};
}
```

**Add methods:**
```cpp
[[nodiscard]] size_t num_keys() const noexcept { return key_count_; }
[[nodiscard]] size_t range_size() const noexcept { return key_count_; }
```

**Update `statistics()`** to not count fingerprint bytes. Or replace with `bits_per_key()` and `memory_bytes()` that only count bucket/offset structures.

**Update builder `build()`:**
- Remove all `fingerprint64()` calls
- Remove overflow collection
- If keys can't all be placed perfectly (collision in a bucket that can't be resolved), retry with a new seed (loop up to 50 attempts, bump seed each time)
- On success, don't store fingerprints. Just buckets + offsets.

**Update `serialize()`/`deserialize()`:**
- Remove fingerprint and overflow vectors from serialization format
- Only serialize: header, seed, key_count, num_buckets, buckets (split + num_keys), bucket_offsets

- [ ] **Step 4: Rewrite CHD (`chd_hasher`)**

Same pattern as RecSplit:
- Remove `fingerprints_`, `overflow_fingerprints_`, `overflow_slots_`, `perfect_count_`
- `slot_for()` returns `slot_index`: hash key to bucket, apply displacement, return slot
- Remove `hash()`, `is_perfect_for()`, `max_slots()`. Add `num_keys()`, `range_size()`.
- Builder retries with new seed if displacement table construction fails
- Serialization: only header + seed + displacement table

- [ ] **Step 5: Rewrite BBHash (`bbhash_hasher`)**

Same pattern:
- Remove `fingerprints_`, `overflow_fingerprints_`, `overflow_slots_`, `perfect_count_`
- `slot_for()` returns `slot_index`: try each level, find first level where bit is set, return cumulative_offset + rank. If no level has the bit set, return an arbitrary index (e.g., `slot_index{0}`).
- Builder: if keys remain after all levels, retry with bumped gamma (increase by 0.5, up to gamma=5.0) or new seed
- Serialization: only header + levels (bits + rank_checkpoints + seeds) + gamma

- [ ] **Step 6: Rewrite FCH (`fch_hasher`)**

Same pattern as CHD (similar displacement-based structure).

- [ ] **Step 7: Rewrite PTHash (`pthash_hasher`)**

Same pattern. Keep the small-key-set limitation. Remove fingerprints and overflow.

- [ ] **Step 8: Remove convenience factory functions that use old interface**

At the bottom of `hashers_perfect.hpp`, there are `make_recsplit()`, `make_bbhash()`, `make_pthash()` factory functions. Update these to match the new builder interface, or remove them if they're thin wrappers.

- [ ] **Step 9: Add static_asserts for concept satisfaction**

At the bottom of `hashers_perfect.hpp`, after the convenience aliases, add:
```cpp
static_assert(perfect_hash_function<recsplit_hasher<8>>);
static_assert(perfect_hash_function<chd_hasher>);
static_assert(perfect_hash_function<bbhash_hasher<3>>);
static_assert(perfect_hash_function<fch_hasher>);
static_assert(perfect_hash_function<pthash_hasher<98>>);
```

- [ ] **Step 10: Build to verify compilation**

```bash
cd /home/spinoza/github/released/maph/build && make -j$(nproc) 2>&1 | grep "error:" | head -20
```

Fix any compilation errors. The tests will fail because they reference old interfaces; that's expected and fixed in Task 3.

- [ ] **Step 11: Commit**

```bash
git add include/maph/hashers_perfect.hpp
git commit -m "refactor: rewrite all 5 algorithms to satisfy perfect_hash_function

RecSplit, CHD, BBHash, FCH, PTHash now return slot_index (non-optional)
from slot_for(). Fingerprint storage, overflow handling, and
is_perfect_for() removed. Builds retry on failure instead of using
overflow. All satisfy perfect_hash_function concept."
```

---

### Task 3: Rewrite Perfect Hash Tests

**Files:**
- Rewrite: `tests/v3/test_perfect_hash.cpp`
- Rewrite: `tests/v3/test_perfect_hash_extended.cpp`

- [ ] **Step 1: Rewrite `tests/v3/test_perfect_hash.cpp`**

Replace the entire file. The new tests follow the same pattern as `test_phobic.cpp`: verify bijectivity, space efficiency, serialization, determinism. For each algorithm:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <maph/hashers_perfect.hpp>
#include <maph/phf_concept.hpp>
#include <random>
#include <algorithm>
#include <set>

using namespace maph;

namespace {
// Copy make_keys() and verify_bijectivity() from test_phobic.cpp
// (same helper functions)

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

template<typename PHF>
bool verify_bijectivity(const PHF& phf, const std::vector<std::string>& keys) {
    std::set<uint64_t> seen;
    for (const auto& key : keys) {
        auto slot = phf.slot_for(key);
        if (slot.value >= phf.range_size()) return false;
        if (!seen.insert(slot.value).second) return false;
    }
    return seen.size() == keys.size();
}

} // namespace

// ===== CONCEPT SATISFACTION =====
static_assert(perfect_hash_function<recsplit_hasher<8>>);
static_assert(perfect_hash_function<chd_hasher>);
static_assert(perfect_hash_function<bbhash_hasher<3>>);
static_assert(perfect_hash_function<fch_hasher>);
static_assert(perfect_hash_function<pthash_hasher<98>>);
```

Then for EACH algorithm, write these test cases (showing RecSplit as example; repeat the pattern for CHD, BBHash, FCH, PTHash with appropriate builder calls):

```cpp
// ===== RECSPLIT TESTS =====

TEST_CASE("RecSplit: bijectivity", "[recsplit]") {
    SECTION("Small key set") {
        std::vector<std::string> keys = {"alpha", "beta", "gamma", "delta", "epsilon"};
        auto phf = recsplit_hasher<8>::builder{}.add_all(keys).build();
        REQUIRE(phf.has_value());
        REQUIRE(verify_bijectivity(*phf, keys));
        REQUIRE(phf->num_keys() == 5);
        REQUIRE(phf->range_size() >= 5);
    }

    SECTION("1000 keys") {
        auto keys = make_keys(1000);
        auto phf = recsplit_hasher<8>::builder{}.add_all(keys).build();
        REQUIRE(phf.has_value());
        REQUIRE(verify_bijectivity(*phf, keys));
    }
}

TEST_CASE("RecSplit: space efficiency", "[recsplit]") {
    auto keys = make_keys(10000);
    auto phf = recsplit_hasher<8>::builder{}.add_all(keys).build();
    REQUIRE(phf.has_value());
    INFO("RecSplit bits per key: " << phf->bits_per_key());
    REQUIRE(phf->bits_per_key() > 0.0);
    // Without fingerprints, RecSplit should be much smaller
    REQUIRE(phf->bits_per_key() < 100.0);
}

TEST_CASE("RecSplit: serialization round-trip", "[recsplit]") {
    auto keys = make_keys(500);
    auto phf = recsplit_hasher<8>::builder{}.add_all(keys).build();
    REQUIRE(phf.has_value());

    auto bytes = phf->serialize();
    auto restored = recsplit_hasher<8>::deserialize(bytes);
    REQUIRE(restored.has_value());
    REQUIRE(restored->num_keys() == phf->num_keys());

    for (const auto& key : keys) {
        REQUIRE(restored->slot_for(key).value == phf->slot_for(key).value);
    }
}

TEST_CASE("RecSplit: works with perfect_filter", "[recsplit]") {
    auto keys = make_keys(500);
    auto phf = recsplit_hasher<8>::builder{}.add_all(keys).build();
    REQUIRE(phf.has_value());

    auto pf = perfect_filter<recsplit_hasher<8>, 16>::build(std::move(*phf), keys);
    for (const auto& key : keys) {
        REQUIRE(pf.contains(key));
        REQUIRE(pf.slot_for(key).has_value());
    }
}
```

Repeat the same 4 test cases (bijectivity, space, serialization, perfect_filter) for:
- `chd_hasher::builder{}.add_all(keys).build()`
- `bbhash_hasher<3>::builder{}.add_all(keys).with_gamma(2.0).build()`
- `fch_hasher::builder{}.add_all(keys).build()`
- `pthash_hasher<98>::builder{}.add_all(keys).build()` (use smaller key sets, 50 keys max)

Include the `#include <maph/perfect_filter.hpp>` at the top.

- [ ] **Step 2: Rewrite or simplify `tests/v3/test_perfect_hash_extended.cpp`**

This file has extended edge-case and stress tests. Update it to use the new interface:
- Replace `slot_for()` optional checks with direct `slot_index` access
- Remove `is_perfect_for()` calls
- Remove overflow-related tests
- Remove `perfect_hash_stats` references (use `bits_per_key()` and `memory_bytes()` directly)

If the file is mostly overflow/fingerprint tests, it may be simpler to delete it and fold any useful tests into the main test file.

- [ ] **Step 3: Build and run**

```bash
cd /home/spinoza/github/released/maph/build && make -j$(nproc) test_v3_perfect_hash && ./tests/v3/test_v3_perfect_hash --reporter compact
```

Expected: All tests pass. If algorithms fail to build (e.g., RecSplit can't find collision-free splits for 1000 keys), debug the retry logic in the algorithm.

- [ ] **Step 4: Commit**

```bash
git add tests/v3/test_perfect_hash.cpp tests/v3/test_perfect_hash_extended.cpp
git commit -m "test: rewrite perfect hash tests for new PHF concept

Tests verify bijectivity, space efficiency, serialization, and
perfect_filter composition for all 5 algorithms. Old fingerprint
and overflow tests removed."
```

---

### Task 4: Update Upper Layers (hashers.hpp, optimization.hpp, maph.hpp)

**Files:**
- Modify: `include/maph/hashers.hpp`
- Modify: `include/maph/optimization.hpp`
- Modify: `include/maph/maph.hpp`
- Modify: `tests/v3/test_hashers.cpp`
- Modify: `tests/v3/test_integration.cpp`

- [ ] **Step 1: Update `minimal_perfect_hasher` in hashers.hpp**

The `minimal_perfect_hasher` class needs to satisfy `perfect_hash_function`:
- Change `slot_for()` to return `slot_index` (not optional). For unknown keys, return `slot_index{total_slots_.value}` or any index.
- Remove `is_perfect_for()`
- Remove `hash()` method (the old hasher concept method)
- Remove `max_slots()`, add `num_keys()` and `range_size()` (both return `total_slots_`)
- Keep `serialize()`/`deserialize()`
- Add `bits_per_key()` and `memory_bytes()`

- [ ] **Step 2: Update `hybrid_hasher` in hashers.hpp**

The `hybrid_hasher` combines a perfect hasher with a fallback. With the new concept, the perfect hasher's `slot_for()` always returns a slot. The hybrid's logic changes:
- It no longer needs to check `is_perfect_for()`. Instead, it can try the perfect hasher for all keys (since it always returns something) and fall back to the standard hasher for collision resolution in the table layer.
- Simplify or remove if it no longer serves a purpose with the new architecture.

- [ ] **Step 3: Update optimization.hpp**

The `optimizer` and `journaled_table` classes reference `minimal_perfect_hasher` directly. Update method signatures to match the new interface (no optional returns, `num_keys()`/`range_size()` instead of `max_slots()`).

- [ ] **Step 4: Update maph.hpp**

Update the high-level `maph` class to work with the updated `minimal_perfect_hasher` and `linear_probe_hasher`. The `optimize()` path uses `minimal_perfect_hasher`; update that flow.

- [ ] **Step 5: Update test_hashers.cpp**

Remove tests for `is_perfect_for()`, update `slot_for()` assertions from optional to direct, update `max_slots()` to `num_keys()`/`range_size()`.

- [ ] **Step 6: Update test_integration.cpp**

Fix any references to old interfaces. These tests exercise end-to-end workflows using the table + optimization layers.

- [ ] **Step 7: Build and run all tests**

```bash
cd /home/spinoza/github/released/maph/build && make -j$(nproc) 2>&1 | grep "error:" | head -20
./tests/v3/test_v3_hashers --reporter compact
./tests/v3/test_v3_table --reporter compact
./tests/v3/test_v3_integration --reporter compact
```

- [ ] **Step 8: Commit**

```bash
git add include/maph/hashers.hpp include/maph/optimization.hpp include/maph/maph.hpp tests/v3/test_hashers.cpp tests/v3/test_integration.cpp
git commit -m "refactor: update upper layers for new PHF concept

minimal_perfect_hasher satisfies perfect_hash_function.
hybrid_hasher, optimizer, journaled_table, and maph updated.
Template constraints removed from linear_probe_hasher and table."
```

---

### Task 5: Update Remaining Tests (properties, comprehensive)

**Files:**
- Modify: `tests/v3/test_properties.cpp`
- Modify: `tests/v3/CMakeLists.txt` (if comprehensive target needs updating)

- [ ] **Step 1: Update test_properties.cpp**

Search for references to old interfaces (`is_perfect_for`, `max_slots`, `overflow`, `perfect_count`, `perfect_hash_stats`). Update or remove.

- [ ] **Step 2: Build and run all tests**

```bash
cd /home/spinoza/github/released/maph/build && ctest --output-on-failure -R "v3_" 2>&1 | tail -30
```

Note which tests pass and which fail. The comprehensive target compiles all test files together, so it will pick up all changes.

- [ ] **Step 3: Fix any remaining compilation issues**

Address any remaining failures in `test_v3_comprehensive`, `test_v3_properties`, or other test targets.

- [ ] **Step 4: Commit**

```bash
git add -u tests/v3/
git commit -m "test: update remaining tests for PHF concept migration"
```

---

### Task 6: Fair Benchmark

**Files:**
- Modify: `benchmarks/bench_phobic.cpp` (rename to `bench_all_phf.cpp` or extend)
- Modify: `benchmarks/bench_perfect_hash_compare.cpp`
- Modify: `benchmarks/CMakeLists.txt`

- [ ] **Step 1: Update bench_perfect_hash_compare.cpp**

This benchmark compares all PHF algorithms. Update it to use the new interface:
- `slot_for()` returns `slot_index` (not optional)
- Remove `is_perfect_for()` checks
- Remove `perfect_hash_stats`; use `bits_per_key()` and `memory_bytes()` directly
- Add a `perfect_filter` variant for each algorithm (16-bit fingerprints) to show the "with membership verification" cost

- [ ] **Step 2: Update bench_phobic.cpp**

The existing benchmark already compares PHOBIC against the existing algorithms. Update the existing-algorithm sections to use the new interface (same changes: non-optional slot_for, direct bits_per_key).

- [ ] **Step 3: Build and smoke-test**

```bash
cd /home/spinoza/github/released/maph/build && cmake .. -DBUILD_TESTS=ON -DBUILD_BENCHMARKS=ON && make -j$(nproc) bench_perfect_hash_compare bench_phobic 2>&1 | tail -10
./benchmarks/bench_perfect_hash_compare 10000
./benchmarks/bench_phobic 10000
```

- [ ] **Step 4: Commit**

```bash
git add benchmarks/
git commit -m "feat: update benchmarks for fair PHF comparison

All algorithms now report pure hash structure bits/key (no fingerprints).
Added perfect_filter<16> variant for each to show membership verification cost."
```

---

### Task 7: Full Regression Check + Benchmark Run

**Files:** None (verification only)

- [ ] **Step 1: Run all tests**

```bash
cd /home/spinoza/github/released/maph/build && ctest --output-on-failure -R "v3_" 2>&1 | tail -30
```

- [ ] **Step 2: Run new test suites specifically**

```bash
./tests/v3/test_v3_phf_concept --reporter compact
./tests/v3/test_v3_phobic --reporter compact
./tests/v3/test_v3_perfect_filter --reporter compact
./tests/v3/test_v3_perfect_hash --reporter compact
./tests/v3/test_v3_membership --reporter compact
```

- [ ] **Step 3: Run fair benchmark at scale**

```bash
./benchmarks/bench_perfect_hash_compare 10000 100000
```

Verify:
- BBHash bits/key ~6 (pure hash structure, no fingerprints)
- RecSplit bits/key significantly lower than the old 160
- All algorithms produce valid bijectivity
- PHOBIC is competitive

- [ ] **Step 4: Commit any final fixes**

```bash
git add -u && git commit -m "fix: address issues found during regression testing"
```
