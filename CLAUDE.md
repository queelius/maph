# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository Overview

**maph** (Memory-Mapped Approximate Perfect Hash) is a high-performance, memory-mapped database built on perfect hash functions. It provides O(1) lookups with sub-microsecond access times for arbitrary key-value mappings. The project is currently in **v3**, which is a complete rewrite using C++23 with concepts, std::expected, and a composable architecture.

## Architecture Overview

### Core Design Philosophy (v3)

The v3 architecture follows these principles:
- **Composability**: Each component does one thing well
- **Type Safety**: Strong types replace primitive obsession
- **Error Handling**: std::expected for elegant error propagation
- **Concepts**: Clear interfaces defined using C++20/23 concepts
- **Zero-Copy**: Memory-mapped storage for minimal overhead
- **Lock-Free**: Atomic operations for concurrent access

### Key Components (include/maph/)

1. **core.hpp**: Fundamental types and concepts
   - Strong types: `slot_index`, `hash_value`, `slot_count`
   - Error handling via `std::expected<T, error>`
   - Core concepts: `hasher`, `storage_backend`

2. **hashers.hpp**: Hash function implementations
   - FNV-1a hasher with configurable slot count
   - Linear probe hasher (decorator pattern)
   - Basic minimal_perfect_hasher (simple implementation)
   - Hybrid hasher (combines perfect and standard hashing)
   - Interface defined by `hasher` concept

3. **hashers_perfect.hpp**: Production-ready perfect hash implementations
   - **RecSplit**: Simplified bucket-split algorithm (~50-100 bits/key in current impl due to fingerprint storage)
   - **CHD**: Classic Compress-Hash-Displace algorithm
   - **BBHash**: Multi-level collision resolution with O(1) rank queries
   - **FCH**: Fox-Chazelle-Heath algorithm
   - **PTHash**: Works for small key sets only (known limitation)
   - Policy-based design with `perfect_hash_builder` concept

4. **storage.hpp**: Storage backends
   - Memory-mapped file storage (zero-copy)
   - In-memory storage (testing)
   - Interface defined by `storage_backend` concept

5. **table.hpp**: Hash table implementation
   - Combines hasher + storage
   - Linear probing for collision resolution
   - Statistics tracking (load factor, probes)

6. **optimization.hpp**: Perfect hash optimization
   - Converts dynamic hash tables to perfect hash
   - Guarantees O(1) lookups with no collisions

7. **maph.hpp**: High-level convenience interface
   - Type-erased wrapper for flexibility
   - Familiar get/set/remove API

### Layered Architecture

```
┌─────────────────────────────────────┐
│  Applications (CLI, REST API)       │
├─────────────────────────────────────┤
│  High-level Interface (maph.hpp)    │
├─────────────────────────────────────┤
│  Hash Table (table.hpp)             │
├─────────────────────────────────────┤
│  Hashers (hashers.hpp) + Storage    │
│  (storage.hpp)                      │
├─────────────────────────────────────┤
│  Core Types (core.hpp)              │
└─────────────────────────────────────┘
```

## Common Development Commands

### Building the Project

```bash
# Configure for v3 (requires C++23)
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DBUILD_EXAMPLES=ON

# Build all targets
make -j$(nproc)

# Build specific targets
make maph                    # Header-only library
make v3_demo                 # Demo application
make v3_simple_test         # Simple test
make hybrid_architecture_demo
```

### Running Tests

```bash
# Run all tests via CTest
cd build && ctest --verbose --output-on-failure

# Run specific test suites (from build directory)
./tests/v3/test_v3_core
./tests/v3/test_v3_hashers
./tests/v3/test_v3_storage
./tests/v3/test_v3_table
./tests/v3/test_v3_integration
./tests/v3/test_v3_properties
./tests/v3/test_v3_perfect_hash

# Run tests with specific tags using Catch2
./tests/v3/test_v3_comprehensive "[core]"
./tests/v3/test_v3_comprehensive "[hasher]"
./tests/v3/test_v3_comprehensive "[storage]"
./tests/v3/test_v3_perfect_hash "[recsplit]"
./tests/v3/test_v3_perfect_hash "[bbhash]"
./tests/v3/test_v3_perfect_hash "[chd]"
./tests/v3/test_v3_perfect_hash "[fch]"

# Run single test by name
./tests/v3/test_v3_perfect_hash "BBHash: Small key set"
```

### Test Coverage

```bash
# Build with coverage enabled
cmake .. -DBUILD_TESTS=ON -DENABLE_COVERAGE=ON
make -j

# Generate coverage report
make v3_coverage             # Full HTML report in build/coverage/html/
make v3_coverage_check       # Quick text report
```

### Running Benchmarks

```bash
# Build benchmarks
cmake .. -DBUILD_BENCHMARKS=ON
make -j

# Run individual benchmarks (from build directory)
./benchmarks/bench_latency              # Single-threaded latency
./benchmarks/bench_throughput           # Multi-threaded throughput
./benchmarks/bench_ycsb                 # YCSB workloads
./benchmarks/bench_comparison           # vs std::unordered_map
./benchmarks/bench_perfect_hash         # Perfect hash optimization
./benchmarks/bench_perfect_hash_compare # Compare all PH algorithms

# Perfect hash comparison with custom key counts
./benchmarks/bench_perfect_hash_compare 1000 10000 100000
```

### Building REST API Server

```bash
cmake .. -DBUILD_REST_API=ON
make -j
./integrations/rest_api/maph_server_v3
```

### Memory Checking

```bash
# Run tests under Valgrind
make test_v3_memcheck

# Build with sanitizers
cmake .. -DBUILD_TESTS=ON -DENABLE_SANITIZERS=ON
make -j
./tests/v3/test_v3_comprehensive
```

## Version History

- **v1**: Original implementation (Jan 2024) - see git tag `v1.0.0`
- **v2**: Perfect hash focus (Mid 2024) - see git branch `v2.0.1`
- **v3**: Complete rewrite with C++23, concepts, composable architecture (Sept 2024) - **CURRENT**

**Important**: The codebase is v3-only. v1/v2 are preserved in git history but removed from the working directory.

## Testing Strategy

### Test Organization (tests/v3/)

| File | Purpose |
|------|---------|
| `test_core.cpp` | Core types, strong types, error handling |
| `test_hashers.cpp` | Hash function implementations |
| `test_storage.cpp` | Storage backends (mmap, memory) |
| `test_table.cpp` | Hash table operations |
| `test_integration.cpp` | End-to-end workflows |
| `test_properties.cpp` | Property-based testing |
| `test_perfect_hash.cpp` | Perfect hash algorithms |

### Framework

- Uses **Catch2 v3** framework
- Test discovery via `catch_discover_tests()`
- Organized with test tags: `[core]`, `[hasher]`, `[storage]`, `[perfect]`, `[recsplit]`, `[bbhash]`, etc.

### Coverage Goals

- Target: >90% code coverage
- Run coverage regularly: `make v3_coverage`
- Coverage reports in: `build/coverage/html/`

## Important Notes

### C++ Standard Requirements

- **v3 requires C++23** (uses concepts from C++20 and std::expected from C++23)
- GCC 13+ or Clang 16+ recommended
- Template metaprogramming for zero-cost abstractions

### Header-Only Library

- `maph` is a header-only library (INTERFACE target)
- All implementation in headers under `include/maph/`
- No separate compilation needed
- Changes require recompilation of dependents

### Memory-Mapped Storage

- Uses `mmap()` for zero-copy file access
- Atomic operations for thread-safety
- 512-byte aligned slots for cache efficiency
- Persistent across process restarts

### Performance Characteristics

- **GET**: 10M ops/sec, <100ns latency (O(1) with perfect hash)
- **SET**: 8M ops/sec, <150ns latency (lock-free atomic)
- **Batch operations**: 50M+ ops/sec with SIMD
- **BBHash queries**: ~12ns (optimized with O(1) rank structure)
- **RecSplit queries**: ~22-30ns
- **CHD queries**: ~29ns

### Perfect Hash Algorithm Status

| Algorithm | Status | Notes |
|-----------|--------|-------|
| RecSplit | ✅ Working | Simplified impl, ~50-100 bits/key |
| CHD | ✅ Working | Classic algorithm, well-tested |
| BBHash | ✅ Working | Optimized with O(1) rank queries |
| FCH | ✅ Working | Simple, educational |
| PTHash | ⚠️ Limited | Works for small sets only (<100 keys) |

## Perfect Hash Functions

### Using Perfect Hash

```cpp
#include <maph/hashers_perfect.hpp>

std::vector<std::string> keys = {"key1", "key2", "key3"};

// RecSplit - Good for most cases
auto recsplit = recsplit8::builder{}
    .add_all(keys)
    .with_seed(12345)
    .build().value();

// BBHash - Fast queries
auto bbhash = bbhash3::builder{}
    .add_all(keys)
    .with_gamma(2.0)
    .build().value();

// CHD - Classic, reliable
auto chd = chd_hasher::builder{}
    .add_all(keys)
    .with_lambda(5.0)
    .build().value();

// FCH - Simple and educational
auto fch = fch_hasher::builder{}
    .add_all(keys)
    .with_bucket_size(4.0)
    .build().value();

// Use any hasher
auto slot = recsplit.slot_for("key1");  // O(1) lookup
auto stats = recsplit.statistics();
std::cout << "Bits per key: " << stats.bits_per_key << std::endl;
```

### Testing Perfect Hash

```bash
# Run all perfect hash tests
./tests/v3/test_v3_perfect_hash

# Run specific algorithm tests
./tests/v3/test_v3_perfect_hash "[bbhash]"
./tests/v3/test_v3_perfect_hash "[recsplit]"
./tests/v3/test_v3_perfect_hash "[chd]"
./tests/v3/test_v3_perfect_hash "[fch]"

# Benchmark comparison
./benchmarks/bench_perfect_hash_compare
```

### Implementing a New Perfect Hash Algorithm

1. Implement the hasher class with builder pattern in `include/maph/hashers_perfect.hpp`
2. Satisfy `perfect_hasher` and `perfect_hash_builder` concepts
3. Add tests in `tests/v3/test_perfect_hash.cpp`
4. Add to benchmark in `benchmarks/bench_perfect_hash_compare.cpp`
5. See `docs/PERFECT_HASH_DESIGN.md` for full specification

## Common Tasks

### Adding a New Test

1. Create test file in `tests/v3/test_myfeature.cpp`
2. Update `tests/v3/CMakeLists.txt`:
   ```cmake
   add_executable(test_v3_myfeature test_myfeature.cpp)
   target_link_libraries(test_v3_myfeature PRIVATE maph Catch2::Catch2WithMain pthread)
   catch_discover_tests(test_v3_myfeature TEST_PREFIX "v3_myfeature:")
   ```
3. Run tests: `ctest -R v3_myfeature`

### Implementing a New Hasher

1. Implement `hasher` concept in `include/maph/hashers.hpp`
2. Required interface:
   ```cpp
   struct my_hasher {
       hash_value hash(std::string_view key) const;
       slot_count max_slots() const;
   };
   ```
3. Add tests in `tests/v3/test_hashers.cpp`

### Implementing a New Storage Backend

1. Implement `storage_backend` concept in `include/maph/storage.hpp`
2. Required interface: `read_slot()`, `write_slot()`, `sync()`, `close()`
3. Add tests in `tests/v3/test_storage.cpp`

### Debugging Build Issues

```bash
# Verbose CMake output
cmake .. -DCMAKE_VERBOSE_MAKEFILE=ON

# Check C++20/23 support
g++ --version
clang++ --version

# Debug build with symbols
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Check what files are being compiled
make VERBOSE=1
```

## Coding Standards

- **Naming**: Classes `PascalCase`, functions `snake_case`, constants `UPPER_SNAKE_CASE`, members `member_name_`
- **Formatting**: 4-space indent, 100 char max line length
- **Style**: RAII, `std::unique_ptr` over raw pointers, `const` everywhere possible

## Commit Message Convention

Follow conventional commits (as per CONTRIBUTING.md):
- `feat:` - New features
- `fix:` - Bug fixes
- `docs:` - Documentation changes
- `test:` - Test additions/changes
- `refactor:` - Code refactoring
- `perf:` - Performance improvements
- `chore:` - Maintenance tasks

## Related Files

- `README.md`: User-facing documentation
- `CONTRIBUTING.md`: Contributor guidelines, coding standards
- `CHANGELOG.md`: Version history
- `docs/PERFECT_HASH_DESIGN.md`: Perfect hash architecture specification
