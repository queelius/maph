# maph Version History

## Overview

maph has gone through three major architectural iterations, each representing a significant evolution in design philosophy and implementation.

## Version Timeline

```
v0.7-0.9  →  v1.0  →  v2.0  →  v3.0  →  v3.1  →  v3.2 (current)
(2023)     (Jan 2024) (Mid 2024) (Sep 2024) (Dec 2024) (Feb 2025)
```

## v3.2.0 (February 2025)

### Added
- Serialization/deserialization for all perfect hash algorithms (RecSplit, CHD, BBHash, FCH, PTHash)
- Parallel construction support for RecSplit via `with_threads(n)`
- SIMD-optimized overflow lookup using AVX2
- Fingerprint-based membership verification

### Fixed
- Serialization now uses fixed-width `uint64_t` for cross-platform portability
- Added bounds validation on deserialized data to prevent OOM from crafted input
- Added endianness static_assert for serialization safety
- Removed dead Golomb-Rice encoding code
- Removed misleading `with_threads()` from BBHash builder (levels are inherently sequential)
- Added thread-safety documentation to `cached_storage`

## Version 1 (v1.0.0) - Original Implementation

**Git Tag**: `v1.0.0`
**Released**: January 2024
**C++ Standard**: C++17
**Status**: Superseded by v3

### Architecture

- **Design**: Monolithic class with dual-mode operation
- **Hashing**: FNV-1a standard hash → Perfect hash optimization
- **Storage**: Memory-mapped with fixed 512-byte slots
- **Concurrency**: Lock-free reads, external sync for writes

### Key Features

```cpp
namespace maph {
    static constexpr uint32_t CURRENT_VERSION = 1;

    class Maph {
        // Single unified implementation
        // Standard hash with linear probing initially
        // Switches to perfect hash after optimization
    };
}
```

- Single slot array (no static/dynamic split)
- SIMD-accelerated batch operations (AVX2)
- Durability manager for periodic syncing
- Basic perfect hash optimization
- REST API server
- CLI tool (`maph_cli`)

### Limitations

- Monolithic design made extensions difficult
- Perfect hash integration was bolted on
- Mixed concerns (storage + hashing + optimization)
- Hard to test components independently

## Version 2 (v2.0) - Perfect Hash Focus

**Git Branch**: `v2.0.1`
**Released**: Mid 2024
**C++ Standard**: C++17
**Status**: Superseded by v3

### Architecture

- **Design**: Enhanced monolithic with better perfect hash
- **Hashing**: Pluggable perfect hash (RecSplit, CHD, BBHash)
- **Storage**: Memory-mapped with key journal
- **Perfect Hash**: `include/maph/perfect_hash.hpp` abstraction

### Key Features

```cpp
namespace maph {
    static constexpr uint32_t CURRENT_VERSION = 2;

    // Introduced perfect hash abstraction
    enum class PerfectHashType {
        RECSPLIT,   // RecSplit minimal perfect hash
        CHD,        // Compress, Hash, Displace
        BBHASH,     // BBHash parallel construction
        DISABLED    // Standard FNV-1a
    };

    class PerfectHashInterface {
        virtual std::optional<uint64_t> hash(JsonView key) = 0;
        virtual uint64_t max_hash() const = 0;
        // ...
    };
}
```

### Improvements over v1

- Dedicated perfect hash interface
- Support for multiple algorithms (RecSplit, CHD, BBHash)
- Key journal for rebuilding perfect hash
- Better optimization workflow
- Backward compatibility with v1 databases

### Perfect Hash Implementations

From v2 (see git history for `include/maph/perfect_hash.hpp`):

```cpp
class RecSplitHash : public PerfectHashInterface {
    // RecSplit minimal perfect hash
    // Fastest queries, ~1.8 bits/key
};

class StandardHash : public PerfectHashInterface {
    // Fallback FNV-1a hashing
};

// CHD and BBHash also available
```

### Limitations

- Still monolithic at core
- Testing required full database setup
- Hard to compose different storage backends
- Perfect hash was separate subsystem, not integrated

## Version 3 (v3.0) - Complete Rewrite

**Location**: `include/maph/`
**Released**: September 2024
**C++ Standard**: C++20/23 (concepts, std::expected)

### Philosophy Change

v3 represents a **complete architectural rewrite** based on modern C++ design principles:

1. **Composability**: Each component does one thing well
2. **Policy-Based**: Use concepts for compile-time polymorphism
3. **Type Safety**: Strong types replace primitives
4. **Error Handling**: std::expected instead of error codes
5. **Testability**: Each component testable independently

### Architecture

```
include/maph/
├── core.hpp           # Strong types, concepts, error handling
├── hashers.hpp        # Standard hash functions
├── hashers_perfect.hpp # Perfect hash implementations (NEW!)
├── storage.hpp        # Storage backends (mmap, memory)
├── table.hpp          # Hash table combining hasher + storage
├── optimization.hpp   # Perfect hash optimization
└── maph.hpp          # High-level convenience interface
```

### Key Innovations

#### 1. Concept-Based Design

```cpp
// Define clear interfaces with concepts
template<typename H>
concept hasher = requires(H h, std::string_view key) {
    { h.hash(key) } -> std::convertible_to<hash_value>;
    { h.max_slots() } -> std::convertible_to<slot_count>;
};

template<typename P>
concept perfect_hasher = hasher<P> && requires(P p, std::string_view key) {
    { p.is_perfect_for(key) } -> std::convertible_to<bool>;
    { p.slot_for(key) } -> std::convertible_to<std::optional<slot_index>>;
};
```

#### 2. Strong Types

```cpp
struct slot_index { uint64_t value; };
struct hash_value { uint64_t value; };
struct slot_count { uint64_t value; };
```

#### 3. Modern Error Handling

```cpp
template<typename T>
using result = std::expected<T, error>;

using status = std::expected<void, error>;
```

#### 4. Composable Components

```cpp
// Mix and match components
auto table = hash_table{
    recsplit8::builder{}.add_all(keys).build().value(),
    mmap_storage{"data.maph", slot_count{1000}}
};
```

#### 5. Policy-Based Perfect Hash

```cpp
// RecSplit with configurable leaf size
template<size_t LeafSize = 8>
class recsplit_hasher {
    class builder;  // Fluent builder interface
};

// CHD with configurable lambda
class chd_hasher {
    class builder;
};

// Easy to add new algorithms!
```

### Components

#### Core Types (`core.hpp`)
- Strong types: `slot_index`, `hash_value`, `slot_count`
- Concepts: `hasher`, `perfect_hasher`, `storage_backend`
- Error handling via `std::expected`

#### Hashers (`hashers.hpp`, `hashers_perfect.hpp`)
- FNV-1a hasher
- Linear probe decorator
- **RecSplit** perfect hash (~2 bits/key)
- **CHD** perfect hash (classic algorithm)
- Hybrid hasher (perfect + standard fallback)

#### Storage (`storage.hpp`)
- Memory-mapped storage
- In-memory storage (testing)
- Clean interface via concepts

#### Table (`table.hpp`)
- Generic hash table
- Takes any hasher + storage
- Statistics tracking
- Thread-safe reads

### Testing Strategy

v3 introduces comprehensive testing:

```
tests/v3/
├── test_core.cpp           # Core types and concepts
├── test_hashers.cpp        # Hash function tests
├── test_storage.cpp        # Storage backend tests
├── test_table.cpp          # Table integration tests
├── test_integration.cpp    # End-to-end tests
├── test_properties.cpp     # Property-based tests
└── test_perfect_hash.cpp   # Perfect hash tests (NEW!)
```

### Benchmarking

```
benchmarks/
├── bench_latency.cpp
├── bench_throughput.cpp
├── bench_ycsb.cpp
├── bench_comparison.cpp
├── bench_perfect_hash.cpp
└── bench_perfect_hash_compare.cpp  # NEW!
```

## Migration Paths

### v1 → v2

- Database format changed (requires export/import)
- API mostly compatible
- Perfect hash support added

### v2 → v3

- **Complete rewrite** - no direct migration
- Architecture fundamentally different
- Concepts replace inheritance
- Components are composable
- Must rewrite applications using new API

### Why v3?

The v3 rewrite addressed fundamental issues:

1. **Testability**: v1/v2 components couldn't be tested in isolation
2. **Composability**: Couldn't mix different storage + hashing strategies
3. **Extensibility**: Adding new algorithms required modifying core classes
4. **Type Safety**: Primitives led to bugs (mixing slot indices with hash values)
5. **Modern C++**: Leverage concepts, std::expected, policy-based design

## Performance Comparison

| Metric | v1 | v2 | v3 |
|--------|----|----|-----|
| Lookup (perfect) | 30-50ns | 25-40ns | 20-40ns |
| Lookup (standard) | 50-100ns | 50-100ns | 30-60ns |
| Build time | Medium | Medium | Fast |
| Memory overhead | High | Medium | Low |
| Space efficiency | 512 bytes/slot | 512 bytes/slot | Configurable |
| Perfect hash space | N/A | ~3 bits/key | 1.8-2.5 bits/key |

## Code Size Comparison

```
v1: ~2,000 lines (single file)
v2: ~3,500 lines (with perfect hash)
v3: ~1,200 lines (split across 6 focused files)
```

v3 achieves more functionality with less code through better abstraction!

## Current Status (December 2024)

- **v1**: Archived, not maintained
- **v2**: Archived, not maintained
- **v3.0**: Stable release - complete rewrite with composable architecture
- **v3.1**: Current release - hybrid perfect hash with overflow handling

## Future (v4?)

Possible future directions:

- Distributed hash tables
- ACID transactions
- GPU acceleration
- Quantum-resistant hashing
- Persistent memory (PMEM) support

## Historical Code

All v1 and v2 code is preserved in git history:
- **v1.0.0 tag**: Original implementation (January 2024)
- **v2.0.1 branch**: Perfect hash focus (Mid 2024)
- **Commit 917a616**: v3.0 complete rewrite (September 2024)

Use `git log` and `git checkout` to explore historical implementations.

## References

- **v1 Release**: Git tag `v1.0.0` (January 2024)
- **v2 Release**: Git branch `v2.0.1` (Mid 2024)
- **v3.0 Release**: Git commit `917a616` "Release v3.0: Complete rewrite" (September 2024)
- **v3.1 Release**: Git tag `v3.1.0` "Hybrid perfect hash with overflow handling" (December 2024)

## Credits

- v1: Initial monolithic design
- v2: Perfect hash integration, multiple algorithms
- v3: Complete rewrite with modern C++, composable architecture
