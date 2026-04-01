# Migrate All Algorithms to Clean PHF Concept

## Problem

The five existing perfect hash algorithms (RecSplit, CHD, BBHash, FCH, PTHash) use an old interface (`hasher`/`perfect_hasher` concepts) that bakes 64-bit fingerprints and overflow handling into each algorithm. This inflates reported space by 50-100x versus the pure hash structure, prevents fair comparison, and forces fingerprint-related code duplication across all algorithms.

The new `perfect_hash_function` concept (already implemented) and `perfect_filter` composition layer provide a clean separation. This spec migrates everything to the new architecture.

## Goals

1. Rewrite all five algorithms in `hashers_perfect.hpp` to satisfy `perfect_hash_function`
2. Remove old `hasher` and `perfect_hasher` concepts from `core.hpp`
3. Update `hashers.hpp` (fnv1a, linear_probe, minimal_perfect, hybrid)
4. Update `table.hpp`, `optimization.hpp`, `maph.hpp` for the new concept
5. Update all tests
6. Fair benchmark comparing all algorithms (pure PHF + with perfect_filter)

## Non-Goals

- Changing algorithm internals (hash functions, data structures)
- Performance optimization of the algorithms themselves
- Adding new algorithms

## Changes Per File

### `include/maph/core.hpp`

**Remove:**
- `concept hasher` (lines 69-76)
- `concept perfect_hasher` (lines 93-100)

**Keep:**
- `slot_index`, `hash_value`, `slot_count` strong types
- `error` enum, `result<T>`, `status`
- `storage_backend` concept
- `key`, `value` types
- `slot<Size>` template

### `include/maph/hashers_perfect.hpp`

Rewrite all five algorithms. For each algorithm, the changes follow the same pattern:

**Remove from each:**
- `fingerprints_` member (vector<uint64_t>)
- `overflow_fingerprints_` member
- `overflow_slots_` member
- `perfect_count_` member
- `is_perfect_for()` method
- `hash()` method (the old `hasher` concept method)
- `max_slots()` method
- Fingerprint verification in `slot_for()`
- Overflow fallback in `slot_for()`
- Fingerprint/overflow serialization
- `fingerprint64()` calls during build
- `find_fingerprint_simd()` calls during query

**Change in each:**
- `slot_for()` return type: `optional<slot_index>` becomes `slot_index`
- `slot_for()` logic: just compute the slot, no fingerprint check, no overflow search
- `statistics()` replaced by `bits_per_key()` and `memory_bytes()` (no fingerprint bytes counted)
- Builder: if keys can't all be placed, retry with different seed/parameters (like PHOBIC does). If all retries fail, return error. No overflow collection.
- Serialization: no fingerprint/overflow vectors

**Add to each:**
- `num_keys()` method
- `range_size()` method
- Must satisfy `perfect_hash_function` concept (static_assert in tests)

**Algorithm-specific notes:**

#### RecSplit
- Core structure: buckets with split values + cumulative offsets
- `slot_for()` becomes: hash to bucket, compute local slot using split, add bucket offset
- Build: if a bucket can't find a collision-free split, retry with new seed
- Remove: fingerprint verification after slot lookup

#### CHD
- Core structure: displacement table
- `slot_for()` becomes: hash to bucket, apply displacement, compute slot
- Build: standard compress-hash-displace construction. Fail if displacement can't be found.

#### BBHash
- Core structure: multi-level bitsets + rank checkpoints
- `slot_for()` becomes: try each level, return offset + rank at first level where bit is set. For keys not in the build set, return the result from the last level (arbitrary but valid).
- Build: level-by-level collision resolution. If keys remain after all levels, retry with bumped gamma.

#### FCH
- Core structure: displacement table (similar to CHD)
- Same pattern as CHD.

#### PTHash
- Core structure: pilot table + slot map
- Same pattern: remove fingerprints, change slot_for return type.
- Known limitation: small key sets only. Keep this limitation.

**Also remove from file:**
- `perfect_hash_stats` struct (replaced by per-algorithm `bits_per_key()`/`memory_bytes()`)
- `perfect_hash_builder` concept (replaced by `phf_builder` in `phf_concept.hpp`)
- `find_fingerprint_simd()` function (no longer needed; keep `fingerprint64()` only if used elsewhere, otherwise remove)
- `PERFECT_HASH_MAGIC` and `PERFECT_HASH_VERSION` stay (used by serialization)
- `MAX_SERIALIZED_ELEMENT_COUNT` stays (used by deserialization bounds checking)

### `include/maph/hashers.hpp`

**`fnv1a_hasher`**: This is a general-purpose hash function, not a PHF. It doesn't satisfy `perfect_hash_function` and shouldn't. Keep as-is but remove the `hasher` concept constraint (it's gone). Make it a plain class.

**`linear_probe_hasher<H>`**: Wraps a hash function with probing. Template parameter was `hasher H`; change to unconstrained `typename H` (or add a lightweight concept if needed). This is used by `table.hpp`.

**`minimal_perfect_hasher`**: This is a simple PHF (uses unordered_map internally). Rewrite to satisfy `perfect_hash_function`: `slot_for()` returns `slot_index` (not optional), remove `is_perfect_for()`, remove `hash()`, add `num_keys()`, `range_size()`.

**`hybrid_hasher<P, H>`**: Combines a perfect hasher with a fallback. Template parameter was `perfect_hasher P, hasher H`. Update to use new types. This may need rethinking since the old concept is gone.

### `include/maph/table.hpp`

Template parameter is `template<hasher Hasher, typename Storage>`. Change to `template<typename Hasher, typename Storage>` (unconstrained, or a new lightweight concept for "anything with hash() and max_slots()"). The table is used with `fnv1a_hasher` and `linear_probe_hasher`, not with PHFs directly.

### `include/maph/optimization.hpp`

Uses `minimal_perfect_hasher` directly. Update to use the new interface.

### `include/maph/maph.hpp`

High-level wrapper. Uses `linear_probe_hasher`, `fnv1a_hasher`, `mmap_storage`, `minimal_perfect_hasher`. Update to match new interfaces.

## Testing

### Updated tests for each algorithm

Each algorithm's tests change the same way:
- Remove `is_perfect_for()` assertions
- Change `slot_for()` assertions from optional to direct slot_index
- Remove overflow-related tests
- Add `static_assert(perfect_hash_function<AlgorithmType>)`
- Verify bijectivity: all keys get distinct slots in [0, range_size)
- Verify `num_keys()` and `range_size()` report correct values
- Serialization round-trip (no fingerprints in serialized data)

### New fair benchmark

Update `bench_phobic.cpp` (or create `bench_all_phf.cpp`) to compare all algorithms:
- Pure PHF: bits/key, query latency, build time
- With perfect_filter<16>: same metrics plus FP rate
- Key counts: 10K, 100K, 1M

## File Checklist

| File | Action |
|------|--------|
| `include/maph/core.hpp` | Remove `hasher`, `perfect_hasher` concepts |
| `include/maph/hashers_perfect.hpp` | Rewrite 5 algorithms, remove fingerprint infrastructure |
| `include/maph/hashers.hpp` | Update fnv1a, linear_probe, minimal_perfect, hybrid |
| `include/maph/table.hpp` | Remove `hasher` concept constraint from template |
| `include/maph/optimization.hpp` | Update to new minimal_perfect_hasher interface |
| `include/maph/maph.hpp` | Update to new interfaces |
| `tests/v3/test_core.cpp` | Remove old concept tests |
| `tests/v3/test_hashers.cpp` | Update for new interfaces |
| `tests/v3/test_perfect_hash.cpp` | Rewrite for new PHF interface |
| `tests/v3/test_perfect_hash_extended.cpp` | Rewrite for new PHF interface |
| `tests/v3/test_table.cpp` | Update if affected |
| `tests/v3/test_integration.cpp` | Update if affected |
| `tests/v3/test_properties.cpp` | Update if affected |
| `benchmarks/bench_phobic.cpp` | Add all algorithms for fair comparison |
| `benchmarks/bench_perfect_hash_compare.cpp` | Update for new interface |

## Success Criteria

- All five algorithms satisfy `perfect_hash_function` (verified by static_assert)
- No fingerprint storage in any algorithm
- BBHash achieves ~6 bits/key (pure hash structure)
- RecSplit achieves reasonable bits/key (without 64-bit fingerprint overhead)
- All existing test files compile and pass (with updated assertions)
- Fair benchmark shows pure PHF bits/key for all algorithms
- `perfect_filter<Algorithm, 16>` works with all five algorithms

## Task Decomposition

This is a large refactor touching ~4200 lines across 15+ files. Recommended task order:

1. **core.hpp**: remove old concepts
2. **hashers.hpp**: update fnv1a, linear_probe, minimal_perfect, hybrid
3. **table.hpp + optimization.hpp + maph.hpp**: update upper layers
4. **hashers_perfect.hpp (BBHash)**: rewrite under new concept
5. **hashers_perfect.hpp (RecSplit)**: rewrite under new concept
6. **hashers_perfect.hpp (CHD)**: rewrite under new concept
7. **hashers_perfect.hpp (FCH)**: rewrite under new concept
8. **hashers_perfect.hpp (PTHash)**: rewrite under new concept
9. **Remove shared fingerprint infrastructure**: perfect_hash_stats, find_fingerprint_simd, etc.
10. **Tests**: update all test files
11. **Benchmark**: fair comparison of all algorithms
12. **Regression check**: full test suite

Tasks 4-8 are independent of each other (can be parallelized). Tasks 1-3 must come first. Task 9 comes after 4-8. Tasks 10-12 come last.
