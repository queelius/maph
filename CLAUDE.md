# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

**maph** (Modern Approximate Perfect Hashing) is a C++23 research playground for perfect hash functions and related approximate data structures. The library is organized around concepts: a concept defines a contract; algorithms are interchangeable implementations; benchmarks compare them along concept-relevant axes (bits per key, query latency, build time, false positive rate).

It is not a production library. For Python/R users, see `../phobic/` (standalone C11 package for PHOBIC).

Requires GCC 13+ or Clang 16+ for `std::expected` and C++20 concepts.

## Build and test

```bash
mkdir -p build && cd build
cmake .. -DBUILD_TESTS=ON -DBUILD_BENCHMARKS=ON
make -j$(nproc)

# Run all tests
ctest --output-on-failure

# Run a single test binary or tag
./tests/v3/test_v3_phobic
./tests/v3/test_v3_perfect_hash "[bbhash]"

# Coverage / sanitizers
cmake .. -DBUILD_TESTS=ON -DENABLE_COVERAGE=ON && make -j v3_coverage
cmake .. -DBUILD_TESTS=ON -DENABLE_SANITIZERS=ON && make -j

# Benchmarks
./benchmarks/bench_phobic 10000 100000 1000000
./benchmarks/bench_perfect_hash_compare 1000 10000 100000
```

CMake options: `BUILD_TESTS`, `BUILD_BENCHMARKS`, `ENABLE_COVERAGE`, `ENABLE_SANITIZERS` (all OFF by default).

## Header layout

Header-only. Every `.hpp` has a single responsibility. Adding a new algorithm is one file plus a benchmark entry.

```
include/maph/
    core.hpp                              strong types, error, result<T>
    concepts/
        perfect_hash_function.hpp         slot_for, num_keys, range_size, serialize
        membership_oracle.hpp             verify(key) -> bool, bits_per_key, memory_bytes
        approximate_map.hpp               contains, slot_for -> optional, num_keys, range_size
    detail/
        serialization.hpp                 phf_serial namespace, magic/version constants
        hash.hpp                          phf_remix (splitmix64), phf_hash_with_seed (FNV-1a+seed)
        fingerprint_hash.hpp              membership_fingerprint (for approximate filters)
    algorithms/
        phobic.hpp                        PHOBIC, pilot-based (2024)
        recsplit.hpp                      RecSplit, recursive splitting
        chd.hpp                           Compress-Hash-Displace
        bbhash.hpp                        Multi-level bitsets + rank queries
        fch.hpp                           Fox-Chazelle-Heath displacement
        pthash.hpp                        PTHash (limited to small key sets)
    filters/
        packed_fingerprint.hpp            k-bit fingerprints packed by slot
        xor_filter.hpp                    3-wise xor filter (standalone membership oracle)
        ribbon_filter.hpp                 Homogeneous ribbon retrieval
    composition/
        perfect_filter.hpp                PHF + packed_fingerprint = approximate_map
```

## Concepts

**`perfect_hash_function`**: `slot_for(key) -> slot_index` (non-optional). For keys in the build set, returns a unique slot in `[0, range_size())`. For keys not in the set, returns an arbitrary valid index. No membership verification happens at this layer.

**`membership_oracle`**: `verify(key) -> bool`. Approximate membership with bounded false positive rate. xor_filter and ribbon_filter are standalone oracles.

**`approximate_map`**: `contains(key) -> bool` plus `slot_for(key) -> optional<slot_index>`. Combines a PHF with membership verification. `perfect_filter<PHF, FPBits>` is the canonical instance. Space: `c + log2(1/eps)` bits/key where `c` is the PHF's space. Beats Bloom filters (`1.44 * log2(1/eps)`) when `eps < 2^-(c/0.44)`. At c=2.7 (PHOBIC), that's `eps < 1.4%`.

## Algorithms at a glance (from bench_phobic, pure PHF bits/key)

| Algorithm | Bits/key | Query | Notes |
|-----------|---------:|------:|-------|
| PHOBIC    | ~2.7     | fast  | Best space. Slow build at scale (pilot search). |
| BBHash    | ~6 (theoretical) / ~27 (current 3-level) | fast  | O(1) rank queries. NumLevels=3 too few for >100K. |
| PTHash    | ~81      | fast  | Limited to small key sets (<100 in current impl). |
| RecSplit  | ~96      | med   | Simplified implementation; far from paper's 1.8 bits/key. |
| CHD       | ~134     | med   | Proven reliability, large displacement table. |
| FCH       | ~200     | med   | Educational. |

See `docs/OPTIMIZATION_NOTES.md` for what's left to improve.

## Adding a new algorithm

1. Create `include/maph/algorithms/<name>.hpp` with a class satisfying `perfect_hash_function`.
2. Provide a nested `builder` satisfying `phf_builder`: `.add()`, `.add_all()`, `.with_seed()`, `.build() -> result<T>`.
3. Retry with a different seed on build failure. No overflow, no fingerprints (those come from `composition/perfect_filter.hpp`).
4. Use `detail/serialization.hpp` for the magic/version header; unique `ALGORITHM_ID` (PHOBIC=6).
5. Add `static_assert(perfect_hash_function<your_type>);` at the bottom of the file.
6. Test: follow the pattern in `tests/v3/test_phobic.cpp` (bijectivity, space, determinism, serialization round-trip, `perfect_filter` composition).
7. Benchmark: add a case to `benchmarks/bench_phobic.cpp`.

## Adding a new filter

1. Create `include/maph/filters/<name>.hpp` with a class satisfying `membership_oracle`.
2. `build(keys)` constructs; `verify(key) -> bool` queries.
3. For serialization, follow the conventions in the other filter headers.
4. If the filter is composable with a PHF to form an `approximate_map`, it may also be used via `composition/perfect_filter.hpp` (which is currently specialized to `packed_fingerprint_array`).

## Testing

**Catch2 v3**. One test file per header. Tags: `[core]`, `[phobic]`, `[recsplit]`, `[chd]`, `[bbhash]`, `[fch]`, `[pthash]`, `[packed]`, `[xor]`, `[ribbon]`, `[perfect_filter]`, `[phf_concept]`, `[membership]`.

`test_v3_all_in_one` is an aggregate target that compiles all test sources together (used by coverage).

## Current state

60/60 tests passing. No pre-existing failures (they were all in the removed mmap storage layer).

## Relationship to phobic

`../phobic/` is a standalone Python package with a pure C11 PHOBIC implementation. maph is the C++ research playground; phobic is the production Python artifact. Algorithm work happens in maph; ideas that prove out get ported to phobic if and when they're worth shipping.

## Coding standards

- **Naming**: classes `snake_case` (library convention), functions `snake_case`, constants `UPPER_SNAKE_CASE`, members `member_name_`.
- **Formatting**: 4-space indent, 100-char line limit.
- **Style**: RAII, `const` everywhere, `std::unique_ptr` over raw pointers.
- **Commits**: conventional commits (`feat:`, `fix:`, `docs:`, `test:`, `refactor:`, `perf:`, `chore:`).
