# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

**maph** is a header-only C++23 library for perfect hash functions (PHFs), membership filters, and memory-mapped value storage. A PHF maps n keys to distinct integers in [0, m); maph provides algorithms to build them, a composition layer to add approximate membership testing, and (optionally) an mmap-backed storage layer for key-value workloads.

Requires GCC 13+ or Clang 16+. Uses `std::expected`, C++20 concepts, and (for the storage layer) `mmap()`.

## Build and Test

All commands run from the `build/` directory:

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
make -j$(nproc)

# Run all tests
ctest --output-on-failure -R "v3_"

# Run a single test suite
./tests/v3/test_v3_phobic
./tests/v3/test_v3_perfect_hash
./tests/v3/test_v3_perfect_filter
./tests/v3/test_v3_membership
./tests/v3/test_v3_phf_concept

# Run by Catch2 tag
./tests/v3/test_v3_perfect_hash "[bbhash]"    # [recsplit], [chd], [fch], [pthash]
./tests/v3/test_v3_membership "[packed]"
./tests/v3/test_v3_perfect_filter "[perfect_filter]"

# Coverage and sanitizers
cmake .. -DBUILD_TESTS=ON -DENABLE_COVERAGE=ON && make -j && make v3_coverage
cmake .. -DBUILD_TESTS=ON -DENABLE_SANITIZERS=ON && make -j

# Benchmarks (fair comparison of all algorithms, pure PHF + perfect_filter)
cmake .. -DBUILD_BENCHMARKS=ON && make -j bench_phobic
./benchmarks/bench_phobic 10000 100000 1000000
```

CMake options: `BUILD_TESTS`, `BUILD_BENCHMARKS`, `BUILD_EXAMPLES`, `BUILD_REST_API`, `ENABLE_COVERAGE`, `ENABLE_SANITIZERS` (all OFF by default).

## Architecture

The library is layered. Lower layers know nothing about upper layers:

```
Layer 3: maph.hpp, table.hpp, storage.hpp, optimization.hpp  (mmap key-value storage)
         |
Layer 2: perfect_filter.hpp                                   (PHF + fingerprints)
         |
Layer 1: phobic.hpp, hashers_perfect.hpp, membership.hpp      (PHF algorithms, fingerprint array)
         |
Layer 0: phf_concept.hpp, core.hpp                            (concepts, strong types, errors)
```

### Layer 0: Concepts and core types

**`phf_concept.hpp`** defines the interface every PHF must satisfy:

```cpp
template<typename P>
concept perfect_hash_function = requires(const P p, std::string_view key) {
    { p.slot_for(key) }    -> std::convertible_to<slot_index>;  // NOT optional
    { p.num_keys() }       -> std::convertible_to<size_t>;
    { p.range_size() }     -> std::convertible_to<size_t>;      // m >= n
    { p.bits_per_key() }   -> std::convertible_to<double>;
    { p.memory_bytes() }   -> std::convertible_to<size_t>;
    { p.serialize() }      -> std::convertible_to<std::vector<std::byte>>;
};
```

Critical semantics: `slot_for()` returns a `slot_index` directly, not `std::optional`. For keys in the build set the result is unique and in `[0, range_size())`. For unknown keys the result is arbitrary but still in range. **No fingerprint verification happens at the PHF layer.**

**`core.hpp`** has strong types (`slot_index`, `hash_value`, `slot_count`) wrapping `uint64_t`, the `error` enum, `result<T>` = `std::expected<T, error>`, and the `storage_backend` concept.

### Layer 1: PHF algorithms

Five algorithms satisfy `perfect_hash_function`:

| Header | Algorithm | Pure bits/key | Notes |
|--------|-----------|---------------|-------|
| `phobic.hpp` | PHOBIC | **~2.7** | Pilot-based. Slow build (pilot search is quadratic-ish per bucket), fastest queries. |
| `hashers_perfect.hpp` | RecSplit, CHD, BBHash, FCH, PTHash | 27-200 | Legacy algorithms, rewritten under the new concept. |

Each has a `builder` class with `.add(key)`, `.add_all(keys)`, `.with_seed(n)`, `.build() -> result<PHF>`. Builds **retry on failure** (new seed, sometimes bumped parameters like alpha or gamma) rather than using overflow.

**Important:** These algorithms no longer bake in fingerprints or overflow handling. That is entirely the job of `perfect_filter`. Before today's migration they stored 64-bit fingerprints per key; that's gone.

**`membership.hpp`** contains only `packed_fingerprint_array<FPBits>` (1-32 bit fingerprints in a packed bit array) and `membership_fingerprint()` (SplitMix64 hash for fingerprinting). The `xor_filter`, `ribbon_filter`, and `fingerprint_verifier` classes were moved out for a future separate project.

### Layer 2: Perfect filter composition

**`perfect_filter.hpp`** composes any PHF with a packed fingerprint array:

```cpp
template<perfect_hash_function PHF, unsigned FPBits = 16>
class perfect_filter {
    PHF phf_;
    packed_fingerprint_array<FPBits> fps_;
public:
    static perfect_filter build(PHF phf, const std::vector<std::string>& keys);
    bool contains(std::string_view key) const noexcept;
    std::optional<slot_index> slot_for(std::string_view key) const noexcept;
};
```

Query: compute slot via PHF, verify fingerprint, return slot or nullopt. FP rate is 2^-FPBits for non-member keys.

**Why this design wins:** A perfect filter uses `c + log2(1/epsilon)` bits/key (where `c` is the PHF's bits/key). A Bloom filter needs `1.44 * log2(1/epsilon)`. Perfect filter beats Bloom when `epsilon < 2^-(c/0.44)`. At c=2.7, that's epsilon < 1.4%. As FPR shrinks, the advantage grows because Bloom's 1.44x multiplier is structural; perfect filter pays 1.0x per bit of FPR precision.

### Layer 3: Memory-mapped storage (optional)

`maph.hpp`, `table.hpp`, `storage.hpp`, `optimization.hpp` form the mmap key-value layer. These are currently standalone-compatible via adapter methods (`hash()` and `max_slots()` on `minimal_perfect_hasher` delegate to `slot_for()`/`range_size()`). The table layer still uses a lightweight unconstrained `typename` template parameter rather than a concept; the old `hasher` concept was removed in today's migration.

## Testing

Framework: **Catch2 v3** with `catch_discover_tests()`.

| Test file | What it tests |
|-----------|---------------|
| `test_phf_concept.cpp` | Concept satisfaction (compile-time + mock runtime) |
| `test_phobic.cpp` | PHOBIC algorithm (bijectivity, space, serialization, perfect_filter composition) |
| `test_perfect_hash.cpp` | The 5 legacy algorithms under the new concept |
| `test_perfect_filter.cpp` | PHF + fingerprint composition |
| `test_membership.cpp` | `packed_fingerprint_array` at widths 8, 10, 12, 16, 32 |
| `test_core.cpp`, `test_hashers.cpp`, `test_storage.cpp`, `test_table.cpp`, `test_integration.cpp`, `test_properties.cpp` | Lower-level and integration tests |

Current state: ~184 tests passing. There are **9 pre-existing failures** (mmap_storage error conditions, cached_storage edge cases, integration real-world scenarios, performance scaling) that were failing before today's work and are unrelated to PHF changes.

## Current State (as of 2026-03)

Today's session completed a major migration:

1. **PHOBIC added** as a clean modern PHF (~2.7 bits/key, pilot-based)
2. **`perfect_hash_function` concept** introduced, replacing old `hasher`/`perfect_hasher` concepts
3. **All 5 legacy algorithms rewritten** to satisfy the new concept (stripped fingerprints and overflow)
4. **`perfect_filter` composition layer** added for optional membership verification
5. **`packed_fingerprint_array` relaxed** to support any width in [1, 32] bits
6. **Fair benchmarks** now compare all algorithms with matched fingerprint widths

See `docs/superpowers/specs/` and `docs/superpowers/plans/` for the design and migration history. See `docs/OPTIMIZATION_NOTES.md` for what's left to optimize.

Known weaknesses:
- **PHOBIC build time** is slow at scale (~23s for 100K keys). Pilot search is parallelizable but not yet parallelized in maph's version.
- **BBHash space** can be high (up to 27 bits/key at scale) because 3 levels + gamma retry isn't enough for minimal mode. Increasing NumLevels would help.
- **RecSplit space** is 96 bits/key (the simplified impl). The theoretical 1.8 bits/key requires a tighter encoding scheme.

## Adding a New PHF Algorithm

1. Create a new header (e.g., `include/maph/myalgo.hpp`).
2. Implement a class satisfying `perfect_hash_function` (see `phf_concept.hpp`).
3. Provide a `builder` class with `.add()`, `.add_all()`, `.with_seed()`, `.build() -> result<MyAlgo>`.
4. On build failure, retry with a different seed (loop up to 50-100 attempts). No overflow.
5. Write serialization using `PERFECT_HASH_MAGIC` (0x4D415048) and a unique `ALGORITHM_ID`.
6. Add `static_assert(perfect_hash_function<MyAlgo>);` in the header.
7. Tests follow the pattern in `tests/v3/test_phobic.cpp`: bijectivity, space, determinism, serialization round-trip, `perfect_filter` composition.
8. Add to `benchmarks/bench_phobic.cpp` for fair comparison.

## Related Projects

- **`../phobic/`**: standalone Python package for PHOBIC (C11, no C++, parallel build via pthreads). Built in this session.
- Membership filters (xor_filter, ribbon_filter, bloom) are slated for a separate project (not maph).

## Coding Standards

- **Naming**: classes `snake_case` for algorithm types (follows existing library convention), functions `snake_case`, constants `UPPER_SNAKE_CASE`, members `member_name_`
- **Formatting**: 4-space indent, 100-char line limit
- **Style**: RAII, `std::unique_ptr` over raw pointers, `const` everywhere possible
- **Commits**: conventional commits (`feat:`, `fix:`, `docs:`, `test:`, `refactor:`, `perf:`, `chore:`)
