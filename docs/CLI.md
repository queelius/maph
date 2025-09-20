# Maph CLI Documentation

## Overview

The `maph` command-line interface provides a convenient way to interact with Maph databases from the terminal. It supports all core operations including database creation, key-value manipulation, batch operations, and performance benchmarking.

## Installation

```bash
# Build from source
mkdir build && cd build
cmake ..
make maph_cli

# Install system-wide (optional)
sudo cp maph_cli /usr/local/bin/maph
```

## Command Reference

### Global Synopsis

```
maph <command> [arguments] [options]
```

### Commands

#### create - Create a new database

Creates a new Maph database file with the specified number of slots.

```bash
maph create <file> <slots>
```

**Arguments:**
- `file` - Path to the database file to create
- `slots` - Number of slots to allocate (determines maximum capacity)

**Examples:**
```bash
# Create database with 1 million slots
maph create data.maph 1000000

# Create database with 10,000 slots
maph create small.maph 10000

# Create database in specific directory
maph create /var/lib/myapp/cache.maph 500000
```

#### set - Store a key-value pair

Sets or updates a value for the specified key.

```bash
maph set <file> <key> <value>
```

**Arguments:**
- `file` - Path to the database file
- `key` - Key to store (any string)
- `value` - Value to store (max 496 bytes)

**Examples:**
```bash
# Store simple value
maph set data.maph "user:1" "Alice"

# Store JSON value
maph set data.maph "config" '{"timeout": 30, "retries": 3}'

# Store with special characters
maph set data.maph "path:/usr/bin" "/usr/bin/env"
```

**Exit Codes:**
- 0 - Success
- 1 - Failed (value too large or database full)

#### get - Retrieve a value

Gets the value associated with a key.

```bash
maph get <file> <key>
```

**Arguments:**
- `file` - Path to the database file
- `key` - Key to retrieve

**Output:**
- Prints the value to stdout if found
- Prints "null" if key not found

**Examples:**
```bash
# Get a value
maph get data.maph "user:1"
# Output: Alice

# Get non-existent key
maph get data.maph "nonexistent"
# Output: null

# Use in scripts
value=$(maph get data.maph "config")
if [ "$value" != "null" ]; then
    echo "Config: $value"
fi
```

**Exit Codes:**
- 0 - Key found
- 1 - Key not found

#### remove - Delete a key

Removes a key-value pair from the database.

```bash
maph remove <file> <key>
```

**Arguments:**
- `file` - Path to the database file
- `key` - Key to remove

**Examples:**
```bash
# Remove a key
maph remove data.maph "user:1"
# Output: OK

# Remove non-existent key
maph remove data.maph "nonexistent"
# Output: Not found
```

**Exit Codes:**
- 0 - Key removed successfully
- 1 - Key not found

#### stats - Show database statistics

Displays detailed statistics about the database.

```bash
maph stats <file>
```

**Arguments:**
- `file` - Path to the database file

**Output Format:**
```
Database: data.maph
Total slots: 1000000
Static slots: 800000
Used slots: 42531
Load factor: 0.0425
Memory usage: 512512000 bytes (488.77 MB)
Generation: 85062
```

**Examples:**
```bash
# Show stats
maph stats data.maph

# Parse specific stat in script
used=$(maph stats data.maph | grep "Used slots" | awk '{print $3}')
echo "Database has $used used slots"
```

#### bench - Run single-threaded benchmark

Performs a performance benchmark with sequential operations.

```bash
maph bench <file>
```

**Arguments:**
- `file` - Path to the database file

**Benchmark Operations:**
1. Sequential writes (100,000 operations)
2. Sequential reads (1,000,000 operations)
3. Random reads (1,000,000 operations)
4. Mixed operations (80% read, 20% write)

**Output Example:**
```
Running benchmark on data.maph...

Sequential Write Performance:
  Operations: 100000
  Time: 45.23 ms
  Throughput: 2,210,000 ops/sec
  Latency: 452 ns/op

Sequential Read Performance:
  Operations: 1000000
  Time: 198.45 ms
  Throughput: 5,039,000 ops/sec
  Latency: 198 ns/op
```

#### bench_parallel - Run multi-threaded benchmark

Performs parallel performance benchmarking.

```bash
maph bench_parallel <file> [threads]
```

**Arguments:**
- `file` - Path to the database file
- `threads` - Number of threads (optional, default: CPU cores)

**Examples:**
```bash
# Use all CPU cores
maph bench_parallel data.maph

# Use 8 threads
maph bench_parallel data.maph 8

# Use 4 threads
maph bench_parallel data.maph 4
```

**Output Example:**
```
Running parallel benchmark with 8 threads...

Parallel Write Performance:
  Operations: 800000
  Time: 156.78 ms
  Throughput: 5,102,000 ops/sec
  Latency: 196 ns/op

Parallel Read Performance:
  Operations: 8000000
  Time: 412.34 ms
  Throughput: 19,410,000 ops/sec
  Latency: 51 ns/op
```

#### load_bulk - Import JSONL file

Loads data from a JSONL (JSON Lines) file in parallel.

```bash
maph load_bulk <file> <jsonl> [--threads <n>]
```

**Arguments:**
- `file` - Path to the database file
- `jsonl` - Path to JSONL input file

**Options:**
- `--threads <n>` - Number of threads for parallel loading

**JSONL Format:**
Each line should be a JSON object with "key" and "value" fields:
```json
{"key": "user:1", "value": "{\"name\": \"Alice\", \"age\": 30}"}
{"key": "user:2", "value": "{\"name\": \"Bob\", \"age\": 25}"}
{"key": "product:100", "value": "{\"name\": \"Widget\", \"price\": 29.99}"}
```

**Examples:**
```bash
# Load with default parallelism
maph load_bulk data.maph import.jsonl

# Load with 4 threads
maph load_bulk data.maph import.jsonl --threads 4

# Generate and load test data
cat > test.jsonl << EOF
{"key": "test:1", "value": "value1"}
{"key": "test:2", "value": "value2"}
EOF
maph load_bulk data.maph test.jsonl
```

**Output:**
```
Loading import.jsonl into data.maph...
Using 4 threads
Processed 10000 lines
Successfully stored: 9998
Failed: 2 (duplicate or full)
Time: 23.45 ms
Throughput: 426,439 records/sec
```

#### mget - Get multiple keys

Retrieves multiple keys in a single operation.

```bash
maph mget <file> <key1> [key2] [key3] ...
```

**Arguments:**
- `file` - Path to the database file
- `key1, key2, ...` - Keys to retrieve

**Output Format:**
```
key1: value1
key2: value2
key3: null
```

**Examples:**
```bash
# Get multiple keys
maph mget data.maph "user:1" "user:2" "user:3"

# Use with xargs
echo -e "key1\nkey2\nkey3" | xargs maph mget data.maph

# Parse output
maph mget data.maph "config" "status" | grep "config:" | cut -d: -f2-
```

#### mset - Set multiple key-value pairs

Sets multiple key-value pairs in a single operation.

```bash
maph mset <file> <key1> <value1> [key2] [value2] ...
```

**Arguments:**
- `file` - Path to the database file
- Pairs of keys and values

**Examples:**
```bash
# Set multiple pairs
maph mset data.maph \
    "user:1" "Alice" \
    "user:2" "Bob" \
    "user:3" "Charlie"

# Set configuration values
maph mset data.maph \
    "config:timeout" "30" \
    "config:retries" "3" \
    "config:debug" "false"
```

**Output:**
```
Stored 3 pairs successfully
```

### Options

#### Global Options

These options can be used with any command:

##### --threads <n>
Specify the number of threads for parallel operations.

```bash
maph load_bulk data.maph input.jsonl --threads 8
```

##### --durability <ms>
Enable automatic durability with specified interval in milliseconds.

```bash
maph set data.maph key value --durability 1000  # Sync every second
```

##### --verbose
Enable verbose output for debugging.

```bash
maph --verbose set data.maph key value
```

##### --help
Display help information.

```bash
maph --help
maph create --help
```

## Usage Patterns

### Database Management

```bash
# Create database
maph create myapp.maph 1000000

# Check database size
ls -lh myapp.maph

# Check database stats
maph stats myapp.maph

# Backup database (it's just a file!)
cp myapp.maph myapp.maph.backup
```

### Data Import/Export

```bash
# Export to JSONL
maph dump data.maph > export.jsonl

# Import from JSONL
maph load_bulk data.maph import.jsonl

# Convert from Redis dump
redis-dump | transform_to_jsonl | maph load_bulk data.maph -
```

### Performance Testing

```bash
# Quick benchmark
maph bench data.maph

# Detailed parallel benchmark
maph bench_parallel data.maph 8

# Custom workload test
for i in {1..1000}; do
    maph set data.maph "test:$i" "value$i"
done
time for i in {1..10000}; do
    maph get data.maph "test:$((RANDOM % 1000))"
done
```

### Scripting Examples

#### Health Check Script

```bash
#!/bin/bash
DB="data.maph"

# Check if database exists
if [ ! -f "$DB" ]; then
    echo "ERROR: Database not found"
    exit 1
fi

# Get stats
stats=$(maph stats "$DB")
used=$(echo "$stats" | grep "Used slots" | awk '{print $3}')
total=$(echo "$stats" | grep "Total slots" | awk '{print $3}')
load=$(echo "$stats" | grep "Load factor" | awk '{print $3}')

# Check load factor
if (( $(echo "$load > 0.8" | bc -l) )); then
    echo "WARNING: Database is ${load}% full"
fi

echo "Database healthy: $used/$total slots used"
```

#### Batch Update Script

```bash
#!/bin/bash
DB="data.maph"

# Read updates from file
while IFS=: read -r key value; do
    maph set "$DB" "$key" "$value"
done < updates.txt
```

#### Cache Warmer

```bash
#!/bin/bash
DB="cache.maph"

# Warm cache with frequently accessed keys
cat frequent_keys.txt | while read key; do
    # Try to get to load into OS page cache
    maph get "$DB" "$key" > /dev/null 2>&1
done

echo "Cache warmed with $(wc -l < frequent_keys.txt) keys"
```

## Error Handling

### Common Errors

#### File Not Found
```
Error: Failed to open data.maph
```
**Solution:** Check file path and permissions

#### Value Too Large
```
Error: Value exceeds maximum size (496 bytes)
```
**Solution:** Reduce value size or split across multiple keys

#### Database Full
```
Error: Failed to set (database full)
```
**Solution:** Create larger database or remove unused keys

#### Permission Denied
```
Error: Permission denied
```
**Solution:** Check file permissions: `chmod 664 data.maph`

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Operation failed |
| 2 | Invalid arguments |
| 3 | File error |
| 4 | Database full |

## Performance Tips

1. **Use batch operations** for multiple keys:
   ```bash
   # Slow
   for key in "${keys[@]}"; do
       maph get data.maph "$key"
   done
   
   # Fast
   maph mget data.maph "${keys[@]}"
   ```

2. **Enable durability** only when needed:
   ```bash
   # Critical data
   maph set data.maph important value --durability 1000
   
   # Cache data (no durability needed)
   maph set cache.maph temp value
   ```

3. **Size database appropriately**:
   ```bash
   # Too small = collisions
   maph create data.maph 100  # Bad for 1000 keys
   
   # Right size = fast
   maph create data.maph 2000  # Good for 1000 keys
   ```

## Integration Examples

### Systemd Service

```ini
[Unit]
Description=Maph Cache Warmer
After=network.target

[Service]
Type=oneshot
ExecStart=/usr/local/bin/maph load_bulk /var/lib/myapp/cache.maph /var/lib/myapp/initial_data.jsonl
User=myapp
Group=myapp

[Install]
WantedBy=multi-user.target
```

### Cron Job

```bash
# Backup database daily
0 3 * * * cp /var/lib/myapp/data.maph /backup/data.maph.$(date +\%Y\%m\%d)

# Clean old entries weekly
0 4 * * 0 /usr/local/bin/clean_old_entries.sh
```

### Docker Integration

```dockerfile
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y build-essential cmake
COPY . /app
WORKDIR /app
RUN mkdir build && cd build && cmake .. && make maph_cli
RUN cp build/maph_cli /usr/local/bin/maph

# Create database on container start
CMD ["maph", "create", "/data/cache.maph", "1000000"]
```

## Troubleshooting

### Debug Mode

Set environment variable for debug output:
```bash
MAPH_DEBUG=1 maph get data.maph key
```

### Verify Database Integrity

```bash
# Check magic number
xxd -l 4 data.maph
# Should show: 4d41 5048 (MAPH)

# Check stats consistency
maph stats data.maph | grep -E "(Used|Total) slots"
```

### Performance Analysis

```bash
# Profile with time
time maph bench data.maph

# Profile with perf
perf stat maph bench_parallel data.maph

# Check I/O stats
iostat -x 1 10 & maph bench data.maph
```

## See Also

- [User Guide](USER_GUIDE.md) - Comprehensive usage documentation
- [Architecture](ARCHITECTURE.md) - Internal design and implementation
- [API Reference](../include/maph.hpp) - C++ API documentation
- [REST API](../integrations/rest_api/API.md) - HTTP interface documentation