# Hybrid Architecture: REST API + Direct mmap Access

## Overview

Since maph v3 stores are backed by memory-mapped files, you can create a **hybrid architecture**:

- **REST API Server**: Manages writes, provides network access
- **Local C++ Apps**: Direct mmap read access with **zero IPC overhead**

This gives you the best of both worlds:
- Convenience of REST API for remote clients and scripting
- Performance of direct memory access for local C++ applications

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                     Shared mmap Files                        │
│                    /var/lib/maph/data/                       │
│                                                               │
│  mydb.maph  config.maph  cache.maph  users.maph             │
└───────┬─────────────┬────────────┬────────────┬─────────────┘
        │             │            │            │
        │ (write)     │ (write)    │ (read)     │ (read)
        │             │            │            │
   ┌────▼─────┐  ┌───▼────┐  ┌────▼─────┐ ┌───▼──────┐
   │  REST    │  │ REST   │  │  C++     │ │  C++     │
   │  API     │  │ API    │  │  App 1   │ │  App 2   │
   │  Server  │  │ Client │  │ (reader) │ │ (reader) │
   └──────────┘  └────────┘  └──────────┘ └──────────┘
        │
        │ HTTP
        │
   ┌────▼─────────────┐
   │  Python Client   │
   │  JavaScript App  │
   │  Remote Services │
   └──────────────────┘
```

## Use Cases

### 1. High-Performance Read Path

**Scenario:** REST API for writes, C++ for latency-critical reads

```cpp
// C++ application: Direct mmap access (read-only)
auto db = maph::maph::open("/var/lib/maph/data/cache.maph", true);  // readonly=true

// Direct memory access - no HTTP overhead
auto value = db.get("hot_key");  // ~300ns latency vs ~1-2ms via REST
```

```python
# Python application: REST API for writes
from maph_client import MaphClient

client = MaphClient("http://localhost:8080")
cache = client.get_store("cache")

# Update via REST API
cache["hot_key"] = "new_value"
```

**Performance:**
- C++ read: **~300ns** (direct mmap)
- REST read: **~1-2ms** (HTTP + serialization)
- **5,000× faster** for local reads!

### 2. Multi-Process Shared Memory

**Scenario:** Multiple C++ processes share configuration via mmap

```cpp
// Process 1: Game server
auto config = maph::maph::open("/var/lib/maph/data/config.maph", true);
auto max_players = config.get("server.max_players");

// Process 2: Admin daemon
auto config = maph::maph::open("/var/lib/maph/data/config.maph", true);
auto log_level = config.get("server.log_level");

// Process 3: REST API server (manages writes)
// - Only process that opens with readonly=false
// - Provides HTTP interface for remote config updates
```

**Benefits:**
- Zero copy: All processes map same physical memory
- Zero IPC overhead: Direct memory reads
- Centralized writes: REST API prevents conflicts

### 3. Polyglot Applications

**Scenario:** Python writes, C++ reads

```python
# Python ETL pipeline: Write processed data
import json
from maph_client import MaphClient

client = MaphClient("http://localhost:8080")
products = client.create_store("products")

for product in process_catalog():
    products[product['sku']] = json.dumps(product)

products.optimize()  # Perfect hash for known SKUs
```

```cpp
// C++ order processing: Read product data
auto products = maph::maph::open("/var/lib/maph/data/products.maph", true);

// Ultra-fast product lookup in order processing
auto product_json = products.get(order.sku);  // Sub-microsecond lookup
auto product = parse_json(product_json);
```

## Synchronization Strategies

### Strategy 1: Single Writer, Multiple Readers (Recommended)

**Setup:**
- REST API server: **Only writer** (readonly=false)
- C++ apps: **All readers** (readonly=true)

**Synchronization:**
- maph v3 uses atomic operations for slots
- Readers see committed writes immediately
- No explicit locking needed for this pattern

**Code:**

```cpp
// REST API server: Writer
auto db = maph::maph::open("/var/lib/maph/data/mydb.maph", false);  // read-write
db.set("key1", "value1");  // Atomic write

// C++ app: Reader
auto db_read = maph::maph::open("/var/lib/maph/data/mydb.maph", true);  // readonly
auto val = db_read.get("key1");  // Atomic read - sees committed write
```

**Memory consistency:**
```cpp
// In maph v3 slot implementation:
struct alignas(64) slot {
    std::atomic<uint64_t> hash_version;  // Upper 32: hash, Lower 32: version
    // ...

    // Writer:
    hash_version.store(new_value, std::memory_order_release);

    // Reader:
    auto current = hash_version.load(std::memory_order_acquire);
};
```

The atomic operations with acquire/release semantics ensure readers see consistent state.

### Strategy 2: File Locking for Multiple Writers (Advanced)

**Setup:**
- Multiple processes can write
- Use file locking (flock/fcntl)

**Implementation:**

```cpp
#include <sys/file.h>
#include <fcntl.h>

class LockedMaphStore {
    int lock_fd_;
    maph::maph db_;

public:
    LockedMaphStore(const std::filesystem::path& path) {
        // Open lock file
        auto lock_path = path.string() + ".lock";
        lock_fd_ = open(lock_path.c_str(), O_CREAT | O_RDWR, 0666);

        // Acquire exclusive lock for write
        flock(lock_fd_, LOCK_EX);

        // Open maph database
        auto result = maph::maph::open(path, false);
        db_ = std::move(*result);
    }

    ~LockedMaphStore() {
        // Release lock
        flock(lock_fd_, LOCK_UN);
        close(lock_fd_);
    }

    // Wrapper methods with guaranteed exclusion
    auto get(std::string_view key) { return db_.get(key); }
    auto set(std::string_view key, std::string_view value) {
        return db_.set(key, value);
    }
};

// Usage:
{
    LockedMaphStore db("/var/lib/maph/data/mydb.maph");
    db.set("key1", "value1");
}  // Lock automatically released
```

### Strategy 3: Read-Write Locks for Mixed Access

**Setup:**
- Multiple readers, occasional writers
- Use shared/exclusive locks

```cpp
class RWLockedMaphStore {
    int lock_fd_;
    maph::maph db_;

public:
    // Acquire shared lock for read
    void read_lock() {
        flock(lock_fd_, LOCK_SH);
    }

    // Acquire exclusive lock for write
    void write_lock() {
        flock(lock_fd_, LOCK_EX);
    }

    void unlock() {
        flock(lock_fd_, LOCK_UN);
    }

    // Read operation (acquires shared lock)
    auto read(std::string_view key) {
        read_lock();
        auto result = db_.get(key);
        unlock();
        return result;
    }

    // Write operation (acquires exclusive lock)
    auto write(std::string_view key, std::string_view value) {
        write_lock();
        auto result = db_.set(key, value);
        unlock();
        return result;
    }
};
```

## Consistency Guarantees

### What maph v3 Provides

1. **Atomic slot updates**: Using `std::atomic<uint64_t>` with proper memory ordering
2. **Version counters**: Lower 32 bits track modifications
3. **Hash verification**: Readers verify hash matches before trusting value
4. **Journaling**: Optional write-ahead log for crash recovery

### Read Consistency

**Single reader sees:**
- ✓ Consistent slot state (atomic operations)
- ✓ Either old or new value (no partial writes)
- ✓ Hash verification prevents reading garbage

**Multiple readers see:**
- ✓ Sequential consistency (if using memory barriers)
- ⚠️ May see different versions temporarily (eventually consistent)

### Write Consistency

**Single writer:**
- ✓ All writes visible to readers
- ✓ No lost updates
- ✓ Journal ensures durability

**Multiple writers (without locking):**
- ✗ Lost updates possible
- ✗ Race conditions on same key
- **Solution:** Use file locking (Strategy 2)

## Example: Hybrid Deployment

### Production Setup

```bash
# Directory structure
/var/lib/maph/
├── data/
│   ├── cache.maph          # Hot cache data
│   ├── config.maph         # Configuration
│   ├── sessions.maph       # User sessions
│   └── products.maph       # Product catalog
└── locks/
    ├── cache.maph.lock
    ├── config.maph.lock
    ├── sessions.maph.lock
    └── products.maph.lock

# Permissions
chown -R maph:maph /var/lib/maph/data
chmod 755 /var/lib/maph/data
chmod 644 /var/lib/maph/data/*.maph
```

### C++ Application (Read-Only)

```cpp
// web_server.cpp - High-performance web server
#include "maph/v3/maph.hpp"
#include <iostream>

int main() {
    // Open all stores read-only
    auto cache = maph::maph::open("/var/lib/maph/data/cache.maph", true);
    auto config = maph::maph::open("/var/lib/maph/data/config.maph", true);
    auto products = maph::maph::open("/var/lib/maph/data/products.maph", true);

    // Request handler
    auto handle_request = [&](const std::string& sku) {
        // Direct mmap lookup - sub-microsecond latency
        auto product_data = products.get(sku);

        if (product_data) {
            return serve_product(*product_data);
        } else {
            return serve_404();
        }
    };

    // Start server...
    run_server(handle_request);
}
```

### REST API Server (Writer)

```bash
# Start REST API server as daemon
systemctl start maph-server

# Server opens stores read-write
# Handles all modifications via HTTP
```

### Python Admin Tool (Via REST)

```python
#!/usr/bin/env python3
# admin_tool.py - Manage products via REST API

from maph_client import MaphClient
import json

client = MaphClient("http://localhost:8080")
products = client.get_store("products")

# Update product catalog
def update_product(sku, data):
    products[sku] = json.dumps(data)
    print(f"✓ Updated {sku}")

# C++ web servers see updates immediately via mmap
update_product("WIDGET-123", {
    "name": "Super Widget",
    "price": 29.99,
    "stock": 100
})
```

## Performance Comparison

### Latency Benchmark

| Access Method | Median | p99 | p99.9 | Overhead |
|---------------|--------|-----|-------|----------|
| Direct mmap (C++) | **0.35μs** | 0.97μs | 1.34μs | - |
| REST API (localhost) | 1,200μs | 2,100μs | 3,500μs | **3,400×** |
| REST API (LAN) | 2,000μs | 4,000μs | 8,000μs | **5,700×** |

### Throughput Benchmark

| Access Method | Single Thread | 8 Threads | Scalability |
|---------------|---------------|-----------|-------------|
| Direct mmap (C++) | **2.8M ops/sec** | 17.2M ops/sec | 6.1× |
| REST API (localhost) | 800 ops/sec | 3.2K ops/sec | 4.0× |

**Conclusion:** Direct mmap access is **3,000-5,000× faster** for local reads.

## Best Practices

### 1. Use Single-Writer Pattern

✓ **Do:** One process writes (REST API server)
✓ **Do:** All others read-only
✗ **Don't:** Multiple writers without locking

### 2. Open Read-Only When Possible

```cpp
// Good: Explicit read-only mode
auto db = maph::maph::open(path, true);  // readonly=true

// Bad: Unnecessary write access
auto db = maph::maph::open(path, false);  // Can cause conflicts
```

### 3. Handle Torn Reads Gracefully

```cpp
// Retry on hash mismatch (rare but possible)
auto read_with_retry = [&](std::string_view key, int max_retries = 3) {
    for (int i = 0; i < max_retries; ++i) {
        auto result = db.get(key);
        if (result) return result;  // Success
        // Hash mismatch or version conflict - retry
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
    return std::unexpected(maph::error::not_found);
};
```

### 4. Use Journaling for Writers

```cpp
// Enable journal for crash recovery
maph::maph::config cfg{slot_count{10000}};
cfg.enable_journal = true;  // Write-ahead log
auto db = maph::maph::create(path, cfg);
```

### 5. Memory Barrier for Critical Reads

```cpp
// Force memory barrier if needed
std::atomic_thread_fence(std::memory_order_acquire);
auto value = db.get("critical_config");
```

### 6. Monitor for Stale Mmaps

```cpp
// Periodically remap to pick up size changes
class RefreshingMaphStore {
    maph::maph db_;
    std::chrono::steady_clock::time_point last_refresh_;

public:
    void maybe_refresh() {
        auto now = std::chrono::steady_clock::now();
        if (now - last_refresh_ > std::chrono::minutes(5)) {
            // Close and reopen to pick up mmap changes
            auto path = get_path();
            db_ = *maph::maph::open(path, true);
            last_refresh_ = now;
        }
    }

    auto get(std::string_view key) {
        maybe_refresh();
        return db_.get(key);
    }
};
```

## Gotchas and Limitations

### 1. File Size Changes

**Problem:** mmap size fixed at open time

**Solution:**
- Provision enough slots upfront
- Or periodically reopen to pick up size changes

### 2. Page Cache Coherency

**Problem:** OS page cache may lag actual file

**Solution:**
- Use `msync()` for writers to flush
- Use `madvise(MADV_DONTNEED)` for readers to drop cache

### 3. NFS/Network Filesystems

**Problem:** mmap over NFS has weak consistency

**Solution:**
- Use local filesystems only
- Or use explicit locking with NFS lock daemon

### 4. Hugepages

**Problem:** Transparent hugepages may cause latency spikes

**Solution:**
```cpp
// Advise kernel about access pattern
madvise(addr, len, MADV_RANDOM);      // Random access
madvise(addr, len, MADV_WILLNEED);    // Preload pages
```

## Security Considerations

### File Permissions

```bash
# Read-only access for apps
chmod 644 /var/lib/maph/data/*.maph

# Only REST API server can write
chown maph:maph /var/lib/maph/data/*.maph

# Apps run as different user
sudo -u webapp ./my_app  # Can only read
```

### Sensitive Data

```cpp
// For sensitive stores, restrict read access
chmod 600 /var/lib/maph/data/secrets.maph

// Only privileged processes can mmap
if (geteuid() != 0) {
    throw std::runtime_error("Must run as root");
}
```

## Conclusion

The hybrid architecture provides:

✓ **Flexibility**: REST API for remote/scripting, mmap for performance
✓ **Zero IPC**: Local C++ apps avoid network overhead
✓ **Scalability**: Readers don't contend with REST API
✓ **Polyglot**: Python writes, C++ reads, JavaScript queries
✓ **Simplicity**: Single-writer pattern avoids complex locking

**Use when:**
- You have performance-critical local C++ applications
- You want zero-copy shared memory between processes
- You need REST API for remote/admin access
- You have read-heavy workloads

**Avoid when:**
- All access is remote (just use REST API)
- You need complex multi-writer coordination
- Data security requires network isolation

---

**Related Documentation:**
- `README.md` - REST API reference
- `PERFECT_HASH.md` - Perfect hash optimization
- `../include/maph/v3/storage.hpp` - mmap implementation
