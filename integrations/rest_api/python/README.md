# maph v3 Python Client

Python client library for [maph v3](https://github.com/queelius/maph) REST API server.

Provides a clean, Pythonic interface to maph memory-mapped perfect hash databases via HTTP.

## Features

- **Dict-like interface**: Natural Python syntax for key-value operations
- **Type hints**: Full type annotations for better IDE support
- **Error handling**: Custom exceptions for different error conditions
- **JSON support**: Easy serialization/deserialization of complex objects
- **Connection pooling**: Efficient HTTP connections via requests library
- **Lightweight**: Single file, minimal dependencies

## Installation

### From source

```bash
cd integrations/rest_api/python
pip install -e .
```

### Requirements

- Python 3.7+
- requests >= 2.25.0

## Quick Start

### Start the maph server

First, start the maph v3 REST API server:

```bash
# From maph repository root
cd build/integrations/rest_api
./maph_server_v3 8080
```

### Basic usage

```python
from maph_client import MaphClient

# Connect to server
client = MaphClient("http://localhost:8080")

# Create a store
store = client.create_store("mydb")

# Insert data (dict-like)
store["user:1001"] = "Alice Johnson"
store["user:1002"] = "Bob Smith"

# Query data
print(store["user:1001"])  # "Alice Johnson"

# Check existence
if "user:1001" in store:
    print("User exists!")

# Delete
del store["user:1002"]

# Get with default
value = store.get("user:9999", "NOT FOUND")

# Store statistics
stats = store.stats()
print(f"Size: {stats.size} keys")
print(f"Load factor: {stats.load_factor:.1%}")
```

## API Reference

### MaphClient

Main client for connecting to maph server.

#### Constructor

```python
MaphClient(base_url: str = "http://localhost:8080", timeout: float = 10.0)
```

**Parameters:**
- `base_url`: Base URL of the REST API server
- `timeout`: Request timeout in seconds

#### Methods

**`create_store(name: str) -> MaphStore`**

Create a new store.

```python
store = client.create_store("users")
```

Raises `StoreExistsError` if store already exists.

**`get_store(name: str) -> MaphStore`**

Get reference to an existing store (does not verify existence).

```python
store = client.get_store("users")
```

**`list_stores() -> List[StoreInfo]`**

List all stores with statistics.

```python
for info in client.list_stores():
    print(f"{info.name}: {info.size} keys, {info.load_factor:.1%} full")
```

### MaphStore

Interface to a single maph store with dict-like operations.

#### Properties

- `name`: Store name (str)

#### Methods

**`get(key: str, default: Optional[str] = None) -> Optional[str]`**

Get value with default if not found.

```python
value = store.get("key1", "default")
```

**`set(key: str, value: str) -> None`**

Set key-value pair.

```python
store.set("key1", "value1")
```

**`delete(key: str) -> bool`**

Delete a key, returns True if deleted, False if not found.

```python
if store.delete("key1"):
    print("Deleted!")
```

**`contains(key: str) -> bool`**

Check if key exists.

```python
if store.contains("key1"):
    print("Exists!")
```

**`stats() -> StoreStats`**

Get store statistics.

```python
stats = store.stats()
print(f"Size: {stats.size}, Load: {stats.load_factor}")
```

**`optimize() -> None`**

Optimize store to use perfect hash function. Guarantees O(1) lookups with zero collisions for all existing keys.

```python
# Insert known keys
store["config1"] = "value1"
store["config2"] = "value2"
store["config3"] = "value3"

# Optimize to perfect hash
store.optimize()  # Now config1-3 have guaranteed O(1) lookups
```

**Note:** Best used when you have a known, mostly-static set of keys. New keys added after optimization still work but use fallback hash.

#### Dict-like interface

MaphStore supports standard dict operations:

```python
# Get
value = store["key1"]

# Set
store["key1"] = "value1"

# Delete
del store["key1"]

# Contains
if "key1" in store:
    print("Exists!")
```

## Examples

### Storing JSON documents

```python
import json
from maph_client import MaphClient

client = MaphClient()
users = client.create_store("users")

# Store JSON profile
profile = {
    "id": 1001,
    "name": "Alice Johnson",
    "email": "alice@example.com",
    "roles": ["admin", "developer"]
}

users["user:1001"] = json.dumps(profile)

# Retrieve and parse
data = json.loads(users["user:1001"])
print(f"User: {data['name']}, Email: {data['email']}")
```

### Batch operations

```python
from maph_client import MaphClient

client = MaphClient("http://localhost:8080", timeout=30.0)
cache = client.create_store("cache")

# Bulk insert
for i in range(1000):
    cache[f"item:{i}"] = f"value_{i}"

# Bulk query
values = [cache[f"item:{i}"] for i in range(100)]

print(f"Inserted {cache.stats().size} keys")
```

### Perfect hash optimization

```python
from maph_client import MaphClient

client = MaphClient()
config = client.create_store("app_config")

# Insert known, static configuration keys
settings = {
    "db.host": "localhost",
    "db.port": "5432",
    "api.timeout": "30",
    "cache.ttl": "3600",
}

for key, value in settings.items():
    config[key] = value

# Optimize to perfect hash for guaranteed O(1) lookups
config.optimize()

print("✓ All config keys now have zero-collision lookups!")

# New keys still work (using fallback hash)
config["debug.enabled"] = "false"
```

**When to use:**
- Static or mostly-static key sets
- Configuration stores
- Lookup tables
- Read-heavy workloads

**Benefits:**
- Guaranteed O(1) lookups for existing keys
- Zero hash collisions
- Optimal memory layout
- New keys still supported via fallback hash

### Error handling

```python
from maph_client import (
    MaphClient,
    StoreExistsError,
    KeyNotFoundError,
    StoreNotFoundError
)

client = MaphClient()

# Handle store creation
try:
    store = client.create_store("mydb")
except StoreExistsError:
    store = client.get_store("mydb")

# Handle missing keys
try:
    value = store["nonexistent"]
except KeyNotFoundError:
    print("Key not found")

# Or use get() with default
value = store.get("nonexistent", "default")
```

## Exception Hierarchy

- `MaphError` - Base exception
  - `StoreNotFoundError` - Store does not exist
  - `KeyNotFoundError` - Key does not exist
  - `StoreExistsError` - Store already exists

## Performance Tips

### 1. Use connection pooling

The client uses `requests.Session` internally for connection pooling. Reuse the same client instance:

```python
# Good: Reuse client
client = MaphClient()
for i in range(1000):
    store = client.get_store("mydb")
    store[f"key:{i}"] = f"value:{i}"
```

### 2. Batch operations

Group operations together to amortize HTTP overhead:

```python
# Prepare data first
data = {f"key:{i}": f"value:{i}" for i in range(1000)}

# Then insert
for key, value in data.items():
    store[key] = value
```

### 3. Use appropriate timeouts

For bulk operations, increase timeout:

```python
client = MaphClient("http://localhost:8080", timeout=60.0)
```

### 4. Local server

For best performance, run the server on localhost to eliminate network latency.

## Comparison with Direct C++ API

| Feature | Python Client | C++ API |
|---------|--------------|---------|
| Latency | ~1-2ms (HTTP) | ~0.3μs (direct) |
| Throughput | ~1-5K ops/sec | ~2-17M ops/sec |
| Ease of use | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ |
| Network access | Yes | No |
| Installation | `pip install` | CMake build |

**Use Python client when:**
- Remote access needed
- Ease of development matters
- Sub-millisecond latency acceptable

**Use C++ API when:**
- Maximum performance required
- Local-only access
- Willing to compile

## Development

### Running examples

```bash
# Start server first
cd /path/to/maph/build/integrations/rest_api
./maph_server_v3 8080

# In another terminal
cd /path/to/maph/integrations/rest_api/python
python examples/basic_usage.py
python examples/json_storage.py
python examples/batch_operations.py
```

### Running tests

```bash
# Install dev dependencies
pip install pytest pytest-cov

# Run tests
pytest tests/ -v
```

## License

MIT License - See repository LICENSE file for details.

## Contributing

Contributions welcome! Please see the main [maph repository](https://github.com/queelius/maph) for contribution guidelines.

## Support

- **Issues**: https://github.com/queelius/maph/issues
- **Documentation**: https://github.com/queelius/maph/docs
- **Server README**: `../README.md` (REST API server documentation)
