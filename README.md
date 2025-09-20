# maph - Memory-Mapped Approximate Perfect Hash

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/std/the-standard)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Version](https://img.shields.io/badge/version-2.0.1--mmap-green.svg)]()

> **Note: This is the mmap-only release branch (v2.0.1-mmap)**
> This version is optimized for maximum performance with memory-mapped storage, providing zero-copy IPC and persistence. For a more generic version with allocator support (coming soon), see the master branch.

A high-performance, memory-mapped database and generalized framework for space-efficient approximate data structures with O(1) lookups. Built on perfect hash functions, maph provides sub-microsecond access to arbitrary mappings (f: X → Y) with configurable storage and custom decoders.

## Key Features (mmap-only version)

- **O(1) Lookups**: Guaranteed constant-time operations with perfect hash optimization
- **Memory-Mapped Storage**: Zero-copy access via mmap for minimal memory overhead
- **Lock-Free Operations**: Atomic operations for thread-safe concurrent access
- **Zero-Copy IPC**: Multiple processes can share the same hash table without serialization
- **Persistence**: Data automatically persists to disk and survives process restarts
- **OpenMP Parallel**: Multi-threaded construction and batch operations
- **SIMD Optimized**: AVX2/AVX-512 acceleration for hash computations
- **JSON Database**: Built-in daemon for JSON key-value storage with REST API

## Performance

| Operation | Throughput | Latency | Notes |
|-----------|------------|---------|-------|
| GET | 10M ops/sec | <100ns | O(1) with perfect hash |
| SET | 8M ops/sec | <150ns | Lock-free atomic updates |
| Batch GET | 50M ops/sec | - | SIMD optimized |
| Batch SET | 40M ops/sec | - | Parallel processing |
| Scan | 100M items/sec | - | Sequential memory access |

## Quick Start

### C++ Library

```cpp
#include <maph.hpp>

// Create a new maph store
auto store = maph::Maph::create("mystore.maph", 1000000);

// Store JSON key-value pairs
store->set(R"({"user": "alice"})", R"({"score": 95})");

// Retrieve with O(1) lookup
auto value = store->get(R"({"user": "alice"})");
if (value) {
    std::cout << *value << std::endl;  // {"score": 95}
}

// Batch operations for high throughput
std::vector<std::pair<JsonView, JsonView>> batch = {
    {R"({"id": 1})", R"({"name": "item1"})"},
    {R"({"id": 2})", R"({"name": "item2"})"}
};
store->mset(batch);
```

### JSON Database Server

```bash
# Start the maph daemon
./maph_daemon --port 8080 --threads 4

# CLI operations
./maph_cli set mystore '{"key": "user:1"}' '{"name": "Alice", "age": 30}'
./maph_cli get mystore '{"key": "user:1"}'
./maph_cli scan mystore --limit 100

# REST API
curl -X POST http://localhost:8080/stores -d '{"name": "products", "slots": 1000000}'
curl -X PUT 'http://localhost:8080/stores/products/keys/"sku:12345"' \
  -d '{"name": "Laptop", "price": 999}'
curl 'http://localhost:8080/stores/products/keys/"sku:12345"'

# Optimize for perfect hash (O(1) guaranteed)
curl -X POST http://localhost:8080/stores/products/optimize
```


## Architecture

The system is built in layers:

1. **Core Library** (`include/maph.hpp`): Memory-mapped storage with atomic operations
2. **Approximate Maps** (`include/maph/approximate_map.hpp`): Generalized X→Y mappings
3. **Decoders** (`include/maph/decoders.hpp`): Configurable output transformations
4. **Applications**:
   - CLI tool (`maph_cli`): Command-line interface
   - Daemon (`maph_daemon`): Standalone JSON database server
   - REST API (`integrations/rest_api`): HTTP interface with web UI

### Storage Layout

```
File Structure:
┌────────────────┐
│ Header (512B)  │  Magic, version, slot counts, generation
├────────────────┤
│ Slot 0 (512B) │  Hash (32b) + Version (32b) + Data (504B)
├────────────────┤
│ Slot 1 (512B) │  
├────────────────┤
│      ...       │
└────────────────┘
```

## Building

```bash
# Clone the repository
git clone https://github.com/yourusername/maph.git
cd maph

# Build everything
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release \
         -DBUILD_TESTS=ON \
         -DBUILD_REST_API=ON \
         -DBUILD_EXAMPLES=ON
make -j$(nproc)

# Run tests
ctest --verbose

# Install
sudo make install
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| BUILD_TESTS | OFF | Build test suite |
| BUILD_EXAMPLES | OFF | Build example programs |
| BUILD_REST_API | OFF | Build REST API server |
| BUILD_DOCS | OFF | Generate documentation |

## Advanced Usage

### Custom Decoders

Create specialized mappings for your use case:

```cpp
// Threshold filter - maps values to boolean based on threshold
template <typename StorageType>
struct ThresholdDecoder {
    using output_type = bool;
    float threshold;
    
    bool operator()(StorageType value, StorageType max_val) const {
        return (float(value) / max_val) > threshold;
    }
};

// Quantization decoder - maps to discrete levels
template <typename StorageType>
struct QuantizeDecoder {
    using output_type = int;
    int levels;
    
    int operator()(StorageType value, StorageType max_val) const {
        return (value * levels) / (max_val + 1);
    }
};
```

### Parallel Operations

```cpp
// Parallel batch processing with custom thread count
store->parallel_mset(large_batch, 8);  // Use 8 threads

// Parallel scan with visitor pattern
std::atomic<size_t> count{0};
store->parallel_scan([&count](uint64_t idx, uint32_t hash, JsonView value) {
    if (value.size() > 100) {
        count++;
    }
}, 4);  // Use 4 threads
```

### Durability Management

```cpp
// Configure background sync for durability
auto manager = store->start_durability_manager(
    std::chrono::seconds(5),  // Sync every 5 seconds
    1000                       // Or after 1000 operations
);

// Manual sync
store->sync();
```

### Storage Trade-offs

| Storage Type | Bytes/Element | False Positive Rate | Use Case |
|--------------|---------------|-------------------|----------|
| uint8_t | 1 | 0.39% | Large datasets, error-tolerant |
| uint16_t | 2 | 0.0015% | Balanced accuracy/space |
| uint32_t | 4 | ~0% | High accuracy required |
| uint64_t | 8 | 0% | Perfect accuracy, cryptographic |

## Comparison with Other Systems

| System | Lookup | Space | Persistence | Concurrent | Use Case |
|--------|--------|-------|-------------|------------|----------|
| **maph** | O(1) | Optimal | mmap | Lock-free | General K/V, high-perf |
| Redis | O(1) avg | 10x overhead | AOF/RDB | Single-thread | Cache, sessions |
| RocksDB | O(log n) | Compressed | SST files | Multi-thread | Large persistent data |
| Memcached | O(1) avg | In-memory | None | Multi-thread | Simple cache |
| Bloom filter | O(1) | Optimal | Custom | Read-only | Membership only |

## Use Cases

- **High-Performance Cache**: Sub-microsecond lookups for hot data
- **Feature Stores**: ML feature serving with guaranteed latency
- **Session Storage**: Web session data with instant access
- **Approximate Filters**: Bloom filter alternative with configurable accuracy
- **Rate Limiting**: Token bucket implementation with O(1) checks
- **Deduplication**: Fast duplicate detection in data streams
- **Graph Databases**: Adjacency list storage with perfect hashing
- **Time-Series Index**: Fast timestamp to data mapping

## API Documentation

### Core API

```cpp
// Create/Open operations
static std::unique_ptr<Maph> create(path, total_slots, static_slots = 0);
static std::unique_ptr<Maph> open(path, readonly = false);

// Basic operations
std::optional<JsonView> get(JsonView key);
bool set(JsonView key, JsonView value);
bool remove(JsonView key);
bool exists(JsonView key);

// Batch operations
void mget(keys, callback);
size_t mset(key_value_pairs);

// Parallel operations
void parallel_mget(keys, callback, threads = 0);
size_t parallel_mset(pairs, threads = 0);
void parallel_scan(visitor, threads = 0);

// Utilities
Stats stats();
void sync();
void close();
```

## Contributing

We welcome contributions! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

Key areas for contribution:
- Perfect hash function implementations
- Additional decoders for specialized use cases
- Language bindings (Python, Rust, Go, Java)
- Performance optimizations
- Documentation and examples

## Publications

The theoretical foundation for this work:

```bibtex
@inproceedings{rdphfilter2024,
  title={Rate-Distorted Perfect Hash Filters},
  author={...},
  booktitle={...},
  year={2024}
}
```

## License

MIT License - see [LICENSE](LICENSE) for details.

## Acknowledgments

Built on research in perfect hash functions, approximate data structures, and space-efficient algorithms. Special thanks to the authors of CHD, BBHash, and other minimal perfect hash algorithms that inspired this work.