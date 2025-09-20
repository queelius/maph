# Maph User Guide

## Table of Contents
- [Introduction](#introduction)
- [Installation](#installation)
- [Quick Start](#quick-start)
- [Basic Operations](#basic-operations)
- [Advanced Features](#advanced-features)
- [Performance Tuning](#performance-tuning)
- [Best Practices](#best-practices)
- [Troubleshooting](#troubleshooting)
- [Examples](#examples)

## Introduction

Maph is a high-performance key-value database designed for sub-microsecond lookups. It uses memory-mapped I/O and approximate perfect hashing to achieve exceptional read performance while maintaining simplicity.

### When to Use Maph
- **Cache layers** requiring ultra-fast lookups
- **Session stores** with bounded value sizes
- **Read-heavy workloads** with infrequent updates
- **Fixed datasets** where keys are known in advance
- **High-frequency trading** systems needing minimal latency

### Key Features
- **Sub-microsecond lookups** (typically 200-500ns)
- **Zero-copy operations** via string_view
- **Automatic persistence** through memory mapping
- **Lock-free reads** for high concurrency
- **Single-file storage** for easy deployment

## Installation

### Building from Source

```bash
# Clone the repository
git clone https://github.com/yourusername/maph.git
cd maph

# Build with CMake
mkdir build && cd build
cmake ..
make -j$(nproc)

# Install (optional)
sudo make install
```

### Using as a Header-Only Library

Simply copy `include/maph.hpp` to your project:

```cpp
#include "maph.hpp"
```

### CMake Integration

```cmake
# In your CMakeLists.txt
find_package(maph REQUIRED)
target_link_libraries(your_target maph::maph)
```

## Quick Start

### Creating a Database

```cpp
#include "maph.hpp"
#include <iostream>

int main() {
    // Create database with 1 million slots
    auto db = maph::create("mydata.maph", 1000000);
    
    // Store some data
    db->set("user:1", R"({"name": "Alice", "age": 30})");
    db->set("user:2", R"({"name": "Bob", "age": 25})");
    
    // Retrieve data
    if (auto value = db->get("user:1")) {
        std::cout << "Found: " << *value << "\n";
    }
    
    // Database automatically saved on destruction
    return 0;
}
```

### Command-Line Usage

```bash
# Create a new database
maph create data.maph 1000000

# Set a value
maph set data.maph "key1" "value1"

# Get a value
maph get data.maph "key1"

# Show statistics
maph stats data.maph
```

## Basic Operations

### Opening a Database

```cpp
// Read-write mode
auto db = maph::open("data.maph");

// Read-only mode (allows multiple readers)
auto db_ro = maph::open_readonly("data.maph");
```

### Storing Data (set)

```cpp
// Store a JSON value
bool success = db->set("product:123", R"({
    "name": "Widget",
    "price": 29.99,
    "stock": 100
})");

// Check for errors
if (!success) {
    // Value too large or table full
}
```

**Constraints:**
- Maximum value size: 496 bytes
- Keys can be any length (hashed to 32-bit)

### Retrieving Data (get)

```cpp
auto value = db->get("product:123");
if (value) {
    // value is std::optional<std::string_view>
    std::cout << "Product: " << *value << "\n";
} else {
    std::cout << "Key not found\n";
}
```

**Important:** The returned `string_view` is valid until the slot is modified.

### Checking Existence

```cpp
if (db->exists("product:123")) {
    std::cout << "Product exists\n";
}
```

### Removing Data

```cpp
bool removed = db->remove("product:123");
if (removed) {
    std::cout << "Product removed\n";
}
```

### Getting Statistics

```cpp
auto stats = db->stats();
std::cout << "Total slots: " << stats.total_slots << "\n"
          << "Used slots: " << stats.used_slots << "\n"
          << "Load factor: " << stats.load_factor << "\n"
          << "Memory: " << stats.memory_bytes << " bytes\n";
```

## Advanced Features

### Batch Operations

#### Multiple Gets (mget)

```cpp
std::vector<std::string_view> keys = {
    "user:1", "user:2", "user:3"
};

db->mget(keys, [](auto key, auto value) {
    std::cout << key << " => " << value << "\n";
});
```

#### Multiple Sets (mset)

```cpp
std::vector<std::pair<std::string_view, std::string_view>> kvs = {
    {"key1", "value1"},
    {"key2", "value2"},
    {"key3", "value3"}
};

size_t stored = db->mset(kvs);
std::cout << "Stored " << stored << " pairs\n";
```

### Parallel Operations

#### Parallel Get

```cpp
// Use 8 threads for parallel retrieval
db->parallel_mget(keys, 
    [](auto key, auto value) {
        // Process each key-value pair
        // Note: Callback must be thread-safe!
    }, 
    8  // thread count
);
```

#### Parallel Set

```cpp
// Use hardware concurrency for parallel storage
size_t stored = db->parallel_mset(kvs);
```

#### Parallel Scan

```cpp
// Scan entire database with 4 threads
std::atomic<size_t> count{0};
db->parallel_scan(
    [&count](uint64_t index, uint32_t hash, auto value) {
        count.fetch_add(1);
        // Process each entry
    },
    4  // threads
);
```

### Durability Management

```cpp
// Enable automatic sync every 5 seconds
db->enable_durability(std::chrono::seconds(5));

// Force immediate sync
db->sync_now();

// Disable automatic sync
db->disable_durability();
```

### Database Scanning

```cpp
// Visit all non-empty slots
db->scan([](uint64_t index, uint32_t hash, auto value) {
    std::cout << "Slot " << index << ": " << value << "\n";
});
```

## Performance Tuning

### Slot Configuration

When creating a database, consider your workload:

```cpp
// All static slots (best for known key sets)
auto db = maph::create("data.maph", 1000000, 1000000);

// Mixed mode (80% static, 20% dynamic - default)
auto db = maph::create("data.maph", 1000000, 800000);

// More dynamic slots (better collision handling)
auto db = maph::create("data.maph", 1000000, 500000);
```

### Memory Prefetching

For batch operations, maph automatically prefetches:

```cpp
// Manual prefetching for custom patterns
for (auto& key : keys) {
    auto [hash, index] = maph::Hash::compute(key, total_slots);
    __builtin_prefetch(&slots[index], 0, 3);
}
// Then process keys...
```

### SIMD Optimization

Batch operations automatically use AVX2 when available:

```cpp
// Check if AVX2 is being used
if (__builtin_cpu_supports("avx2")) {
    std::cout << "Using SIMD acceleration\n";
}
```

### Thread Count Selection

```cpp
// Automatic (uses hardware concurrency)
db->parallel_mget(keys, callback, 0);

// Manual selection based on workload
size_t threads = keys.size() > 10000 ? 8 : 1;
db->parallel_mget(keys, callback, threads);
```

## Best Practices

### Key Design

1. **Use prefixes** for namespacing:
   ```cpp
   db->set("user:123", userData);
   db->set("product:456", productData);
   ```

2. **Keep keys short** but descriptive:
   ```cpp
   // Good
   db->set("u:123", userData);
   
   // Avoid
   db->set("user_profile_data_for_customer_number_123", userData);
   ```

### Value Management

1. **Use JSON for structured data**:
   ```cpp
   db->set("config", R"({"timeout": 30, "retries": 3})");
   ```

2. **Compress if needed** (before storing):
   ```cpp
   std::string compressed = compress(largeJson);
   if (compressed.size() <= 496) {
       db->set(key, compressed);
   }
   ```

### Concurrency Patterns

1. **Read-only sharing**:
   ```cpp
   // Multiple threads can share read-only handle
   auto db_ro = maph::open_readonly("data.maph");
   ```

2. **Write synchronization**:
   ```cpp
   std::mutex write_lock;
   
   // In write threads
   {
       std::lock_guard<std::mutex> lock(write_lock);
       db->set(key, value);
   }
   ```

### Error Handling

```cpp
// Check database creation
auto db = maph::create("data.maph", 1000000);
if (!db) {
    std::cerr << "Failed to create database\n";
    return 1;
}

// Check value size
if (value.size() > 496) {
    std::cerr << "Value too large\n";
    return 1;
}

// Handle full database
if (!db->set(key, value)) {
    std::cerr << "Database full or value too large\n";
}
```

## Troubleshooting

### Common Issues

#### Database Won't Open
```cpp
// Check file permissions
struct stat st;
if (stat("data.maph", &st) != 0) {
    perror("stat");
}

// Check file format
auto db = maph::open("data.maph");
if (!db) {
    // File corrupted or wrong format
}
```

#### High Memory Usage
- Memory-mapped files appear in virtual memory but not necessarily in RAM
- Use `stats()` to check actual slot usage
- Consider smaller slot count if many are unused

#### Slow Performance
1. Check load factor - resize if > 0.7
2. Ensure prefetching for batch operations
3. Use parallel operations for large batches
4. Consider SSD over HDD for storage

#### Data Loss
- Enable durability for critical data
- Call `sync_now()` before shutdown
- Remember: static slots overwrite on collision

### Debugging

```cpp
// Enable debug output
#define MAPH_DEBUG
#include "maph.hpp"

// Check database integrity
auto stats = db->stats();
assert(stats.used_slots <= stats.total_slots);

// Trace operations
db->scan([](uint64_t idx, uint32_t hash, auto value) {
    std::cerr << "Slot " << idx << " hash=" << std::hex << hash
              << " size=" << value.size() << "\n";
});
```

## Examples

### Session Store

```cpp
class SessionStore {
    std::unique_ptr<maph::Maph> db;
    
public:
    SessionStore(const std::string& path, size_t max_sessions) {
        db = maph::create(path, max_sessions);
    }
    
    void save_session(const std::string& session_id, 
                     const std::string& user_data) {
        std::string key = "session:" + session_id;
        db->set(key, user_data);
    }
    
    std::optional<std::string> get_session(const std::string& session_id) {
        std::string key = "session:" + session_id;
        auto value = db->get(key);
        if (value) {
            return std::string(*value);
        }
        return std::nullopt;
    }
    
    void expire_session(const std::string& session_id) {
        std::string key = "session:" + session_id;
        db->remove(key);
    }
};
```

### Cache Layer

```cpp
class Cache {
    std::unique_ptr<maph::Maph> db;
    std::hash<std::string> hasher;
    
public:
    Cache(size_t capacity) {
        db = maph::create("cache.maph", capacity);
    }
    
    void put(const std::string& key, const std::string& value) {
        if (value.size() > 496) {
            // Value too large for cache
            return;
        }
        db->set(key, value);
    }
    
    std::optional<std::string> get(const std::string& key) {
        auto value = db->get(key);
        if (value) {
            return std::string(*value);
        }
        return std::nullopt;
    }
    
    void clear() {
        db->scan([this](uint64_t idx, uint32_t hash, auto value) {
            // Clear all slots by setting empty
            db->remove_by_index(idx);
        });
    }
};
```

### Bulk Import

```cpp
void import_jsonl(const std::string& db_path, 
                 const std::string& jsonl_path) {
    auto db = maph::open(db_path);
    std::ifstream file(jsonl_path);
    std::string line;
    
    std::vector<std::pair<std::string_view, std::string_view>> batch;
    
    while (std::getline(file, line)) {
        // Parse JSON line to extract key and value
        auto [key, value] = parse_json_line(line);
        batch.emplace_back(key, value);
        
        // Process in batches
        if (batch.size() >= 1000) {
            size_t stored = db->parallel_mset(batch);
            std::cout << "Imported " << stored << " records\n";
            batch.clear();
        }
    }
    
    // Process remaining
    if (!batch.empty()) {
        db->parallel_mset(batch);
    }
}
```

### Monitoring and Metrics

```cpp
class DatabaseMonitor {
    std::unique_ptr<maph::Maph> db;
    
public:
    void print_metrics() {
        auto stats = db->stats();
        
        std::cout << "Database Metrics:\n"
                  << "================\n"
                  << "Total Capacity: " << stats.total_slots << "\n"
                  << "Used Slots: " << stats.used_slots << "\n"
                  << "Free Slots: " << (stats.total_slots - stats.used_slots) << "\n"
                  << "Load Factor: " << std::fixed << std::setprecision(2) 
                  << (stats.load_factor * 100) << "%\n"
                  << "Memory Usage: " << format_bytes(stats.memory_bytes) << "\n"
                  << "Generation: " << stats.generation << "\n";
        
        // Warn if getting full
        if (stats.load_factor > 0.8) {
            std::cerr << "WARNING: Database is " 
                      << (stats.load_factor * 100) << "% full\n";
        }
    }
    
private:
    std::string format_bytes(size_t bytes) {
        const char* units[] = {"B", "KB", "MB", "GB"};
        int unit = 0;
        double size = bytes;
        
        while (size > 1024 && unit < 3) {
            size /= 1024;
            unit++;
        }
        
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2) << size << " " << units[unit];
        return ss.str();
    }
};
```

## Migration Guide

### From Redis

```cpp
// Redis: SET key value
// Maph:
db->set(key, value);

// Redis: GET key
// Maph:
auto value = db->get(key);

// Redis: EXISTS key
// Maph:
bool exists = db->exists(key);

// Redis: DEL key
// Maph:
db->remove(key);

// Redis: MGET key1 key2 key3
// Maph:
db->mget({key1, key2, key3}, callback);
```

### From memcached

```cpp
// memcached: set(key, value, expiry)
// Maph: (no built-in expiry)
db->set(key, value);
// Implement expiry externally if needed

// memcached: get(key)
// Maph:
auto value = db->get(key);

// memcached: delete(key)
// Maph:
db->remove(key);
```

## Performance Benchmarks

Typical performance on modern hardware (Intel Core i7, NVMe SSD):

| Operation | Throughput | Latency |
|-----------|------------|---------|
| get() | 5M ops/sec | 200ns |
| set() | 2M ops/sec | 500ns |
| parallel_mget (8 threads) | 20M ops/sec | 50ns/op |
| scan() | 100M slots/sec | 10ns/slot |

Comparison with other systems:

| System | Get Latency | Set Latency | Persistence |
|--------|------------|-------------|-------------|
| Maph | 200ns | 500ns | Automatic |
| Redis | 1-2µs | 1-2µs | Configurable |
| memcached | 1-2µs | 1-2µs | None |
| RocksDB | 5-10µs | 10-20µs | WAL + SST |

## Conclusion

Maph provides unmatched performance for specific use cases where:
- Read performance is critical
- Value sizes are bounded
- Dataset fits in memory
- Simplicity is valued

For more complex requirements (transactions, complex queries, unbounded values), consider traditional databases.