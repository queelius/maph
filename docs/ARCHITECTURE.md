# Maph Architecture Documentation

## Table of Contents
- [Overview](#overview)
- [System Design](#system-design)
- [Memory Layout](#memory-layout)
- [Perfect Hash Function](#perfect-hash-function)
- [Collision Handling](#collision-handling)
- [Concurrency Model](#concurrency-model)
- [Performance Optimizations](#performance-optimizations)
- [Storage Tiers](#storage-tiers)
- [Durability and Persistence](#durability-and-persistence)

## Overview

Maph (Memory-mapped Approximate Perfect Hash) is a high-performance key-value database that combines memory-mapped I/O with approximate perfect hashing to achieve sub-microsecond lookups. The system is designed for read-heavy workloads where lookup performance is critical.

### Key Design Goals
- **Ultra-fast lookups**: O(1) average case, typically < 1µs
- **Zero-copy operations**: Direct memory access via string_view
- **Persistence**: Automatic durability through mmap
- **Simplicity**: Single-file database, no external dependencies
- **Scalability**: Support for billions of key-value pairs

## System Design

### Architecture Layers

```
┌─────────────────────────────────────────┐
│         Application Layer               │
│   (REST API, CLI, Client Libraries)     │
├─────────────────────────────────────────┤
│           Maph Core API                 │
│  (get, set, remove, batch operations)   │
├─────────────────────────────────────────┤
│         Hash Function Layer             │
│    (FNV-1a, SIMD optimization)          │
├─────────────────────────────────────────┤
│        Storage Management               │
│   (Slot allocation, version control)    │
├─────────────────────────────────────────┤
│      Memory-Mapped I/O (mmap)           │
│    (OS page cache, virtual memory)      │
├─────────────────────────────────────────┤
│          File System                    │
│      (Persistent storage)               │
└─────────────────────────────────────────┘
```

### Component Interactions

1. **Client Request**: Application makes a get/set request
2. **Hash Computation**: Key is hashed using FNV-1a algorithm
3. **Slot Location**: Hash determines slot index via modulo
4. **Memory Access**: Direct memory read/write via mmap
5. **OS Sync**: Operating system handles page cache and disk sync

## Memory Layout

The database file consists of a header followed by a fixed array of slots:

```
┌────────────────┐
│   Header       │ 512 bytes
│   (Metadata)   │
├────────────────┤
│   Slot 0       │ 512 bytes
├────────────────┤
│   Slot 1       │ 512 bytes
├────────────────┤
│      ...       │
├────────────────┤
│   Slot N-1     │ 512 bytes
└────────────────┘
```

### Header Structure (512 bytes)

| Field | Size | Description |
|-------|------|-------------|
| magic | 4 bytes | File format identifier (0x4D415048 = "MAPH") |
| version | 4 bytes | Database format version |
| total_slots | 8 bytes | Total number of slots |
| static_slots | 8 bytes | Number of perfect hash slots |
| generation | 8 bytes | Global modification counter |
| reserved | 480 bytes | Future extensions |

### Slot Structure (512 bytes)

| Field | Size | Description |
|-------|------|-------------|
| hash_version | 8 bytes | Atomic 64-bit: hash (32) + version (32) |
| size | 4 bytes | Value data size |
| reserved | 4 bytes | Future use |
| data | 496 bytes | Actual value storage |

### Memory Alignment

- Slots are 64-byte aligned for optimal CPU cache line usage
- Atomic operations on hash_version prevent torn reads/writes
- Data layout minimizes false sharing between threads

## Perfect Hash Function

### FNV-1a Algorithm

The system uses FNV-1a (Fowler-Noll-Vo) hash function for its simplicity and good distribution:

```cpp
uint32_t fnv1a(const char* data, size_t len) {
    uint32_t hash = 2166136261u;  // FNV offset basis
    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 16777619u;  // FNV prime
    }
    return hash;
}
```

### Hash Properties
- **Distribution**: Excellent avalanche effect
- **Speed**: Single pass, minimal operations
- **Collision Rate**: ~1/2³² for random keys
- **SIMD Support**: AVX2 implementation for batch operations

### Slot Index Calculation

```cpp
slot_index = hash % total_slots
```

For static slots (perfect hashing region):
- Direct mapping: one slot per unique hash
- No collision handling needed
- Single memory access for lookups

## Collision Handling

The database uses a two-tier approach:

### Static Slots (Default: 80% of total)
- **Perfect hashing**: Each key maps to exactly one slot
- **No collisions**: Overwrites on hash collision
- **Use case**: Known key sets, unique identifiers

### Dynamic Slots (Default: 20% of total)
- **Linear probing**: Search up to MAX_PROBE_DISTANCE (10) slots
- **Collision resolution**: Find next empty slot
- **Use case**: General-purpose storage, unknown key sets

### Probing Strategy

```cpp
for (size_t i = 0; i < MAX_PROBE_DISTANCE; i++) {
    uint32_t idx = (base_index + i) % total_slots;
    if (slots[idx].empty() || slots[idx].hash() == target_hash) {
        // Found target slot
    }
}
```

## Concurrency Model

### Thread Safety Guarantees

| Operation | Thread Safety | Notes |
|-----------|--------------|-------|
| get() | ✅ Safe | Lock-free reads via atomic operations |
| set() | ⚠️ Unsafe | Requires external synchronization |
| remove() | ⚠️ Unsafe | Requires external synchronization |
| scan() | ✅ Safe | Read-only operation |
| parallel_* | ✅ Safe | Internal thread management |

### Atomic Operations

The `hash_version` field uses atomic operations for consistency:

1. **Version Increment**: Odd version = updating, Even version = stable
2. **Double-Write Pattern**:
   - Write hash with version+1 (mark as updating)
   - Copy data
   - Write hash with version+2 (mark as complete)

### Lock-Free Reads

Readers can safely access slots without locks:
- Atomic load of hash_version
- Check version parity (skip if odd/updating)
- Read data if version is stable (even)

## Performance Optimizations

### CPU Cache Optimization
- **64-byte alignment**: Fits exactly one cache line
- **Prefetching**: Batch operations prefetch future slots
- **Hot/cold separation**: Metadata separate from data

### SIMD Acceleration
- **AVX2 batch hashing**: Process 8 keys in parallel
- **Automatic detection**: Runtime CPU feature detection
- **Fallback**: Scalar implementation for compatibility

### Memory Access Patterns
- **Sequential scanning**: Predictable access for prefetcher
- **Spatial locality**: Related data in same cache line
- **Temporal locality**: Version check before data access

### Zero-Copy Design
- **string_view returns**: No allocation for reads
- **Direct memory access**: Values read from mmap'd memory
- **In-place updates**: Writes directly to mapped region

## Storage Tiers

### Tier 1: CPU Cache (L1/L2/L3)
- **Latency**: 1-10 ns
- **Size**: MB range
- **Usage**: Hot slots, frequently accessed

### Tier 2: RAM (Page Cache)
- **Latency**: 50-100 ns
- **Size**: GB range
- **Usage**: Active working set

### Tier 3: Disk (SSD/HDD)
- **Latency**: 10-100 µs (SSD), 1-10 ms (HDD)
- **Size**: TB range
- **Usage**: Full database, cold data

The OS transparently manages data movement between tiers via the page cache.

## Durability and Persistence

### Automatic Persistence
- **mmap semantics**: Changes visible immediately in memory
- **OS page cache**: Buffers writes, flushes periodically
- **Crash consistency**: OS ensures atomic page writes

### Explicit Durability Options

1. **sync()**: Request async flush to disk (MS_ASYNC)
2. **sync_now()**: Force synchronous flush (MS_SYNC)
3. **DurabilityManager**: Background thread for periodic sync

### Recovery Semantics
- **No journal/WAL**: Direct updates to data file
- **Atomic slots**: Each slot update is atomic
- **Version checking**: Detect incomplete updates

### Best Practices
- Enable durability for critical data
- Use sync_now() before shutdown
- Monitor generation counter for change detection

## Design Trade-offs

### Advantages
- **Simplicity**: Single file, no complex structures
- **Performance**: Sub-microsecond lookups
- **Zero maintenance**: No compaction, no garbage collection
- **OS integration**: Leverages OS page cache effectively

### Limitations
- **Fixed slot size**: 496 bytes maximum value
- **No compression**: Values stored as-is
- **Limited transactions**: No ACID guarantees
- **Hash collisions**: Possible data loss in static region

### When to Use Maph
✅ **Good fit for:**
- Read-heavy workloads
- Known value size bounds
- Cache/session storage
- High-frequency lookups
- Fixed datasets

❌ **Not ideal for:**
- Large values (>496 bytes)
- Complex queries
- Strong consistency requirements
- Frequent updates to same keys
- Unknown/unbounded datasets

## Future Enhancements

### Planned Improvements
- **Cuckoo hashing**: Better collision handling
- **Compression**: Value compression support
- **Sharding**: Multi-file databases
- **Replication**: Master-slave support
- **Transactions**: Basic MVCC support

### Research Areas
- **Learned indexes**: ML-based hash functions
- **Persistent memory**: Intel Optane support
- **RDMA**: Remote memory access
- **GPU acceleration**: CUDA/OpenCL hashing