# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

**maph** is a header-only C++23 library implementing memory-mapped perfect hash tables with O(1) lookups. v3 is the current and only active version; v1/v2 exist only in git history.

Requires GCC 13+ or Clang 16+. Uses `std::expected`, C++20 concepts, and `mmap()` for zero-copy storage.

## Build and Test

All commands run from the `build/` directory:

```bash
# Configure and build
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
make -j$(nproc)

# Run all tests
ctest --output-on-failure

# Run a single test suite
./tests/v3/test_v3_perfect_hash

# Run by Catch2 tag
./tests/v3/test_v3_perfect_hash "[bbhash]"
./tests/v3/test_v3_comprehensive "[core]"

# Run a single test by name
./tests/v3/test_v3_perfect_hash "BBHash: Small key set"

# Coverage (requires lcov/gcov)
cmake .. -DBUILD_TESTS=ON -DENABLE_COVERAGE=ON && make -j
make v3_coverage           # HTML report -> build/coverage/html/
make v3_coverage_check     # Text summary only

# Sanitizers
cmake .. -DBUILD_TESTS=ON -DENABLE_SANITIZERS=ON && make -j

# Benchmarks
cmake .. -DBUILD_BENCHMARKS=ON && make -j
./benchmarks/bench_perfect_hash_compare 1000 10000 100000
```

CMake options: `BUILD_TESTS`, `BUILD_BENCHMARKS`, `BUILD_EXAMPLES`, `BUILD_REST_API`, `ENABLE_COVERAGE`, `ENABLE_SANITIZERS` (all OFF by default).

## Architecture

Header-only library (`include/maph/`). The `maph` CMake target is INTERFACE, so any header change recompiles all dependents.

### Concept-driven composition

Three core concepts in `core.hpp` define the extension points:
- **`hasher`**: `hash(string_view) -> hash_value` + `max_slots() -> slot_count`
- **`storage_backend`**: `read()`, `write()`, `clear()`, `empty()`, `hash_at()`
- **`perfect_hasher`** (refines `hasher`): adds `is_perfect_for()`, `slot_for()`

Any hasher can be composed with any storage backend via `table.hpp`. The `optimization.hpp` layer converts a dynamic table into a perfect-hash-optimized one. `maph.hpp` provides a type-erased convenience wrapper.

### Perfect hash algorithms (`hashers_perfect.hpp`)

All five algorithms live in one header. Each uses a builder pattern satisfying `perfect_hash_builder`:

| Algorithm | Status | Notes |
|-----------|--------|-------|
| RecSplit  | Working | Default choice. Supports `with_threads(n)` for parallel construction. |
| CHD       | Working | Classic, reliable |
| BBHash    | Working | O(1) rank queries, fast lookups |
| FCH       | Working | Simple, educational |
| PTHash    | Limited | Small key sets only (<100 keys) |

All algorithms support:
- **Overflow handling**: keys that can't be perfectly hashed fall back to fingerprint-based linear search (builds never fail)
- **Serialization**: `serialize() -> vector<byte>`, `deserialize(span<byte>) -> result<T>` (little-endian, magic+version validated)
- **SIMD overflow lookup**: AVX2 fingerprint search with scalar fallback

### Storage layout

512-byte aligned slots. Each slot: 16-byte metadata (atomic hash+version, size, reserved) + 496 bytes data. Memory-mapped via `mmap()` with lock-free atomic operations.

### Strong types

`slot_index`, `hash_value`, `slot_count` wrap `uint64_t` with explicit constructors to prevent mixing. Error handling uses `result<T>` = `std::expected<T, error>`.

## Testing

Framework: **Catch2 v3** with `catch_discover_tests()`. Tags: `[core]`, `[hasher]`, `[storage]`, `[perfect]`, `[recsplit]`, `[bbhash]`, `[chd]`, `[fch]`.

Test executables: `test_v3_core`, `test_v3_hashers`, `test_v3_storage`, `test_v3_table`, `test_v3_integration`, `test_v3_properties`, `test_v3_perfect_hash`, `test_v3_perfect_hash_extended`, `test_v3_comprehensive` (combined).

Custom make targets: `test_v3_all`, `test_v3_fast` (excludes benchmarks), `test_v3_memcheck` (Valgrind).

Coverage target: >90%. Reports in `build/coverage/html/`.

## Adding a New Perfect Hash Algorithm

1. Add to `include/maph/hashers_perfect.hpp` (must satisfy `perfect_hasher` + `perfect_hash_builder` concepts)
2. Include `serialize()`/`deserialize()` with the standard magic/version header
3. Add tests in `tests/v3/test_perfect_hash.cpp`
4. Add to `benchmarks/bench_perfect_hash_compare.cpp`
5. See `docs/PERFECT_HASH_DESIGN.md` for the full specification

## Coding Standards

- **Naming**: classes `PascalCase`, functions `snake_case`, constants `UPPER_SNAKE_CASE`, members `member_name_`
- **Formatting**: 4-space indent, 100-char line limit
- **Style**: RAII, `std::unique_ptr` over raw pointers, `const` everywhere possible
- **Commits**: conventional commits: `feat:`, `fix:`, `docs:`, `test:`, `refactor:`, `perf:`, `chore:`
