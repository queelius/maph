# Perfect Hash Function Design for maph

## Overview

This document describes the policy-based design for perfect hash functions in maph. The design supports multiple algorithms while maintaining clean interfaces via C++20 concepts.

## Goals

1. **Pluggable Algorithms**: Easy to add new perfect hash algorithms
2. **Zero-Cost Abstractions**: Compile-time polymorphism via concepts
3. **Benchmarkable**: Compare different algorithms objectively
4. **Composable**: Works seamlessly with existing v3 architecture
5. **Modern C++**: Leverage C++20/23 features

## Perfect Hash Algorithms

### Supported Algorithms

| Algorithm | Type | Space | Build Time | Query Time | Features |
|-----------|------|-------|------------|------------|----------|
| **RecSplit** | Minimal | 1.8-2.0 bits/key | O(n) | O(1) | Cache-friendly, SIMD |
| **CHD** | Minimal | 2.0-2.5 bits/key | O(n) | O(1) | Classic, well-tested |
| **BBHash** | Minimal | 2.0-3.0 bits/key | O(n) parallel | O(1) | Parallel construction |
| **PTHash** | Minimal | 2.0-2.2 bits/key | O(n) | O(1) | Recent, very fast |
| **FCH** | Minimal | 2.0-2.5 bits/key | O(n log n) | O(1) | Minimal, simple |

### Algorithm Selection Guide

- **RecSplit**: Default choice - fastest queries, good space efficiency
- **CHD**: Legacy systems, proven reliability
- **BBHash**: Large datasets requiring parallel construction
- **PTHash**: When both build and query speed are critical
- **FCH**: Educational purposes, simple implementation

## Architecture

### Core Concept

```cpp
template<typename PH>
concept perfect_hash_builder = requires(PH ph, std::vector<std::string> keys) {
    // Build phase
    { PH::builder{} } -> std::same_as<typename PH::builder>;
    { typename PH::builder{}.add(std::string_view{}) } -> std::same_as<typename PH::builder&>;
    { typename PH::builder{}.build() } -> std::same_as<result<PH>>;

    // Query phase
    { ph.hash(std::string_view{}) } -> std::convertible_to<hash_value>;
    { ph.slot_for(std::string_view{}) } -> std::convertible_to<std::optional<slot_index>>;
    { ph.max_slots() } -> std::convertible_to<slot_count>;
    { ph.is_perfect_for(std::string_view{}) } -> std::convertible_to<bool>;

    // Metadata
    { ph.bits_per_key() } -> std::convertible_to<double>;
    { ph.memory_bytes() } -> std::convertible_to<size_t>;
    { ph.key_count() } -> std::convertible_to<size_t>;
};
```

### Interface Design

Each perfect hash algorithm implements:

1. **Builder Pattern**: Fluent interface for construction
2. **Query Interface**: Fast O(1) lookups
3. **Serialization**: Save/load to disk
4. **Statistics**: Space usage, bits per key

### Example Usage

```cpp
// RecSplit - fastest queries
auto recsplit = recsplit_hasher::builder{}
    .add("key1")
    .add("key2")
    .add("key3")
    .leaf_size(8)  // Optional tuning
    .build()
    .value();

// CHD - classic algorithm
auto chd = chd_hasher::builder{}
    .add_all(keys)
    .lambda(5.0)  // Load factor
    .build()
    .value();

// BBHash - parallel construction
auto bbhash = bbhash_hasher::builder{}
    .add_all(keys)
    .gamma(2.0)
    .threads(8)
    .build()
    .value();

// Use with table
auto table = hash_table{
    recsplit,
    mmap_storage{path, recsplit.max_slots()}
};
```

## Hybrid Perfect Hash with Overflow Handling

All perfect hash algorithms in maph support **graceful overflow handling**. When a key cannot be perfectly placed during construction, it falls back to a fingerprint-based linear search.

### How It Works

1. **Build Phase**: Algorithm attempts to place all keys perfectly
2. **Overflow Collection**: Keys that cannot be placed are collected
3. **Fingerprint Storage**: Overflow keys store 64-bit fingerprints for verification
4. **Sequential Slots**: Overflow keys are assigned sequential slots after perfect keys

### Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                    Perfect Hash Hasher                        │
├──────────────────────────────────────────────────────────────┤
│  slot_for(key):                                              │
│    1. Compute fingerprint = hash64(key)                      │
│    2. Try perfect hash lookup                                │
│       - If found AND fingerprint matches → return slot       │
│    3. Fallback: linear search overflow_fingerprints_         │
│       - If fingerprint matches → return overflow_slots_[i]   │
│    4. Return nullopt (key not in set)                        │
├──────────────────────────────────────────────────────────────┤
│  Internal Storage:                                           │
│    - Perfect hash structure (algorithm-specific)             │
│    - fingerprints_[]: 64-bit hashes for all keys            │
│    - overflow_fingerprints_[]: fingerprints of overflow keys │
│    - overflow_slots_[]: slot assignments for overflow keys   │
│    - perfect_count_: number of perfectly placed keys         │
└──────────────────────────────────────────────────────────────┘
```

### Benefits

- **Build Never Fails**: Construction always succeeds
- **Graceful Degradation**: Only overflow keys have slower lookup
- **Dynamic Compatibility**: Same approach used when adding new keys at runtime
- **Fingerprint Verification**: No false positives for unknown keys

### Statistics

```cpp
auto stats = hasher.statistics();
std::cout << "Total keys: " << stats.key_count << "\n";
std::cout << "Perfect keys: " << stats.perfect_count << "\n";
std::cout << "Overflow keys: " << stats.overflow_count << "\n";
std::cout << "Perfect ratio: " << (100.0 * stats.perfect_count / stats.key_count) << "%\n";
```

### Performance Characteristics

| Scenario | Lookup Time | Notes |
|----------|-------------|-------|
| Perfect key | O(1) | Single hash + verify |
| Overflow key | O(k) | Linear scan of k overflow keys |
| Unknown key | O(1) or O(k) | Depends on hash result |

For typical use cases with <5% overflow, performance impact is negligible.

## Implementation Strategy

### Phase 1: Core Infrastructure

1. Define `perfect_hash_builder` concept
2. Extend existing `perfect_hasher` concept
3. Create benchmark framework

### Phase 2: Algorithm Implementations

1. **RecSplit** (priority 1)
   - Implement recursive splitting algorithm
   - Add SIMD optimizations
   - Support leaf sizes 4-16

2. **CHD** (priority 2)
   - Implement classic CHD algorithm
   - Support configurable lambda (load factor)
   - Add to benchmark suite

3. **BBHash** (priority 3)
   - Implement parallel construction
   - Support gamma parameter
   - Thread pool integration

4. **PTHash** (priority 4)
   - Implement encoder-based design
   - SIMD hash functions
   - Optimization for modern CPUs

### Phase 3: Integration

1. Hybrid hasher support
2. Serialization format
3. Benchmark suite
4. Documentation

## Serialization Format

Each perfect hash function serializes to a common format:

```
Header (32 bytes):
  - Magic: "MAPH" (4 bytes)
  - Version: uint32_t (4 bytes)
  - Algorithm Type: uint8_t (1 byte)
  - Flags: uint8_t (1 byte)
  - Key Count: uint64_t (8 bytes)
  - Max Hash: uint64_t (8 bytes)
  - Reserved: (6 bytes)

Algorithm-Specific Data:
  - Variable length based on algorithm
  - Self-describing format
```

## Benchmarking

### Metrics

1. **Build Time**: Time to construct from n keys
2. **Query Time**: Average/p99 lookup latency
3. **Space**: Bits per key
4. **Cache Misses**: L1/L2/L3 miss rates
5. **Throughput**: Queries per second

### Benchmark Suite

```bash
# Run all perfect hash benchmarks
./bench_perfect_hash --all

# Compare algorithms
./bench_perfect_hash --compare --size 1000000

# Detailed profiling
./bench_perfect_hash --algorithm recsplit --profile
```

## Testing Strategy

### Unit Tests

- Algorithm correctness
- Edge cases (empty, single key, duplicates)
- Serialization round-trip
- Concept compliance

### Integration Tests

- Table integration
- Hybrid hasher composition
- Persistence across restarts

### Property Tests

- No collisions for original keys
- Consistent hash values
- Serialization determinism

## Performance Targets

| Keys | Algorithm | Build Time | Query Time | Space |
|------|-----------|------------|------------|-------|
| 1M | RecSplit | <100ms | <30ns | 2.0 bits/key |
| 1M | CHD | <150ms | <35ns | 2.5 bits/key |
| 1M | BBHash | <50ms (8 threads) | <40ns | 2.5 bits/key |
| 1M | PTHash | <80ms | <25ns | 2.2 bits/key |

## Future Extensions

1. **Dynamic Perfect Hashing**: Support for incremental updates
2. **Compressed Keys**: Store keys with perfect hash for validation
3. **Approximate Perfect Hashing**: Trade-off between space and collision rate
4. **GPU Acceleration**: CUDA/OpenCL for massive datasets
5. **Quantum-Resistant**: Post-quantum secure hash functions

## References

1. RecSplit: [Esposito et al. 2019](https://arxiv.org/abs/1910.06416)
2. CHD: [Belazzougui et al. 2009](https://cmph.sourceforge.net/)
3. BBHash: [Limasset et al. 2017](https://github.com/rizkg/BBHash)
4. PTHash: [Pibiri & Trani 2021](https://github.com/jermp/pthash)
5. FCH: [Fox et al. 1992](https://en.wikipedia.org/wiki/Perfect_hash_function)
