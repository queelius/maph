# maph v3 REST API Server

A high-performance REST API server for maph v3, providing HTTP access to memory-mapped perfect hash databases with sub-microsecond latency.

## Features

- **Multi-store Management**: Host multiple maph databases on one server
- **RESTful API**: Standard HTTP verbs (GET, PUT, DELETE)
- **Thread-Safe**: Concurrent reads with shared_mutex
- **CORS Enabled**: Web browser access supported
- **Persistent Storage**: Databases backed by memory-mapped files
- **JSON Responses**: Clean, structured error messages and data
- **Python Client**: Official Python library for easy integration ([see below](#python-client))
- **Hybrid Architecture**: C++ apps can directly mmap files for zero IPC overhead ([see HYBRID_ARCHITECTURE.md](HYBRID_ARCHITECTURE.md))

---

## Hybrid Architecture (Advanced)

**For maximum performance:** C++ applications can directly read maph stores via mmap while the REST API server manages writes:

```cpp
// C++ app: Direct mmap access (read-only)
auto cache = maph::maph::open("/var/lib/maph/data/cache.maph", true);
auto value = cache.get("hot_key");  // ~300ns vs ~1-2ms via REST
```

**Benefits:**
- **5,000× faster** local reads (~300ns vs ~1-2ms)
- Zero IPC overhead (no network, no serialization)
- Zero-copy memory access
- Still use REST API for remote access and writes

**See:** [HYBRID_ARCHITECTURE.md](HYBRID_ARCHITECTURE.md) for complete guide with examples.

---

## Python Client

A Pythonic client library is available for easy integration with Python applications:

```python
from maph_client import MaphClient

# Connect and create a store
client = MaphClient("http://localhost:8080")
store = client.create_store("mydb")

# Dict-like interface
store["user:1001"] = "Alice Johnson"
print(store["user:1001"])

# JSON support
import json
store["config"] = json.dumps({"theme": "dark", "lang": "en"})
```

**Installation:**

```bash
cd integrations/rest_api/python
pip install -e .
```

**Full documentation:** See [python/README.md](python/README.md)

---

## Installation

### Prerequisites

```bash
# Ubuntu/Debian
sudo apt-get install libmicrohttpd-dev

# Fedora/RHEL
sudo dnf install libmicrohttpd-devel

# macOS
brew install libmicrohttpd
```

### Building

```bash
# From repository root
cd /path/to/maph

# Configure with REST API enabled
mkdir -p build && cd build
cmake .. -DBUILD_REST_API=ON -DCMAKE_BUILD_TYPE=Release

# Build the server
make -j$(nproc) maph_server_v3

# Server binary is at:
# ./integrations/rest_api/maph_server_v3
```

### Installation (Optional)

```bash
# Install to /usr/local/bin
sudo make install
```

---

## Quick Start

### 1. Start the Server

```bash
# Start on default port 8080
./integrations/rest_api/maph_server_v3

# Or specify a custom port
./integrations/rest_api/maph_server_v3 9090
```

Output:
```
Initializing maph v3 REST API server...

✓ maph v3 REST API server running on port 8080

API Endpoints:
  GET    /stores                       - List all stores
  POST   /stores/{name}                - Create a new store
  GET    /stores/{name}/stats          - Get store statistics
  GET    /stores/{name}/keys/{key}     - Get key value
  PUT    /stores/{name}/keys/{key}     - Set key value (body = value)
  DELETE /stores/{name}/keys/{key}     - Delete key

Example Usage:
  curl http://localhost:8080/stores
  curl -X PUT -d 'hello world' http://localhost:8080/stores/test/keys/greeting
  curl http://localhost:8080/stores/test/keys/greeting

Press Enter to stop server...
```

### 2. Create a Store

```bash
# Create a new store named "mydb"
curl -X POST http://localhost:8080/stores/mydb
```

Response:
```json
{"success":true,"message":"Store created"}
```

### 3. Insert Data

```bash
# Set a key-value pair
curl -X PUT -d 'Alice Johnson' \
  http://localhost:8080/stores/mydb/keys/user:1001
```

Response:
```json
{"success":true,"message":"Key set"}
```

### 4. Retrieve Data

```bash
# Get the value for a key
curl http://localhost:8080/stores/mydb/keys/user:1001
```

Response:
```json
{"value":"Alice Johnson"}
```

---

## API Reference

### Base URL

```
http://localhost:8080
```

All responses are JSON with appropriate HTTP status codes.

---

### List All Stores

**Endpoint:** `GET /stores`

**Description:** Returns a list of all active stores with their statistics.

**Example:**
```bash
curl http://localhost:8080/stores
```

**Response:**
```json
[
  {
    "name": "test",
    "size": 3,
    "load_factor": 0.000
  },
  {
    "name": "mydb",
    "size": 42,
    "load_factor": 0.001
  }
]
```

---

### Create a Store

**Endpoint:** `POST /stores/{name}`

**Description:** Creates a new maph database with the specified name.

**Parameters:**
- `{name}` - Store name (alphanumeric, hyphens, underscores)

**Example:**
```bash
curl -X POST http://localhost:8080/stores/sessions
```

**Success Response (201 Created):**
```json
{"success":true,"message":"Store created"}
```

**Error Response (400 Bad Request):**
```json
{"error":"Store already exists"}
```

---

### Get Store Statistics

**Endpoint:** `GET /stores/{name}/stats`

**Description:** Returns statistics about a store.

**Example:**
```bash
curl http://localhost:8080/stores/mydb/stats
```

**Response:**
```json
{
  "size": 42,
  "load_factor": 0.001
}
```

---

### Optimize Store to Perfect Hash

**Endpoint:** `POST /stores/{name}/optimize`

**Description:** Converts an existing store to use a minimal perfect hash function, guaranteeing O(1) lookups with zero collisions for all existing keys. This rebuilds the store's hash table using the current key set.

**Use Case:** After inserting a known, static set of keys, optimize the store for maximum lookup performance.

**Example:**
```bash
# 1. Create store and insert keys
curl -X POST http://localhost:8080/stores/config
curl -X PUT -d 'value1' http://localhost:8080/stores/config/keys/setting1
curl -X PUT -d 'value2' http://localhost:8080/stores/config/keys/setting2
curl -X PUT -d 'value3' http://localhost:8080/stores/config/keys/setting3

# 2. Optimize to perfect hash
curl -X POST http://localhost:8080/stores/config/optimize
```

**Success Response (200 OK):**
```json
{"success":true,"message":"Store optimized to perfect hash"}
```

**Error Responses:**

*Store not found (404):*
```json
{"error":"Store not found"}
```

*Optimization failed (500):*
```json
{"error":"Failed to optimize store"}
```

**Important Notes:**
- Existing keys get guaranteed O(1) lookups with zero collisions
- New keys added after optimization use fallback hash (FNV-1a with linear probing)
- Best used when you have a known, mostly-static set of keys
- Optimization is a one-time operation per key set

---

### Get Key Value

**Endpoint:** `GET /stores/{name}/keys/{key}`

**Description:** Retrieves the value associated with a key.

**Parameters:**
- `{name}` - Store name
- `{key}` - Key to retrieve (URL-encoded)

**Example:**
```bash
curl http://localhost:8080/stores/mydb/keys/user:1001
```

**Success Response (200 OK):**
```json
{"value":"Alice Johnson"}
```

**Error Response (404 Not Found):**
```json
{"error":"Key not found"}
```

**URL Encoding:**
```bash
# Keys with special characters must be URL-encoded
curl http://localhost:8080/stores/mydb/keys/email%40example.com
```

---

### Set Key Value

**Endpoint:** `PUT /stores/{name}/keys/{key}`

**Description:** Sets or updates a key-value pair. Request body is the raw value.

**Parameters:**
- `{name}` - Store name
- `{key}` - Key to set
- **Body:** Raw value data

**Example:**
```bash
# Simple text value
curl -X PUT -d 'Bob Smith' \
  http://localhost:8080/stores/mydb/keys/user:1002

# JSON value
curl -X PUT -H "Content-Type: application/json" \
  -d '{"name":"Bob","age":30}' \
  http://localhost:8080/stores/mydb/keys/user:1002

# Binary data
curl -X PUT --data-binary @photo.jpg \
  http://localhost:8080/stores/mydb/keys/photo:123
```

**Success Response (200 OK):**
```json
{"success":true,"message":"Key set"}
```

**Error Response (400 Bad Request):**
```json
{"error":"No value provided"}
```

---

### Delete Key

**Endpoint:** `DELETE /stores/{name}/keys/{key}`

**Description:** Deletes a key-value pair from the store.

**Example:**
```bash
curl -X DELETE http://localhost:8080/stores/mydb/keys/user:1002
```

**Success Response (200 OK):**
```json
{"success":true,"message":"Key deleted"}
```

**Error Response (404 Not Found):**
```json
{"error":"Key not found"}
```

---

## Advanced Usage

### Storing JSON Documents

```bash
# Store a JSON user profile
curl -X PUT -H "Content-Type: application/json" \
  -d '{
    "id": 1001,
    "name": "Alice Johnson",
    "email": "alice@example.com",
    "roles": ["admin", "developer"]
  }' \
  http://localhost:8080/stores/users/keys/user:1001

# Retrieve and parse with jq
curl http://localhost:8080/stores/users/keys/user:1001 | jq -r '.value'
```

### Batch Operations with Shell Scripts

```bash
#!/bin/bash
# bulk_insert.sh - Insert 1000 keys

for i in {1..1000}; do
  curl -X PUT -d "value_$i" \
    http://localhost:8080/stores/bulk/keys/key_$i
done

echo "Inserted 1000 keys"
```

### Using with Python

**Recommended: Use the official Python client**

```python
from maph_client import MaphClient
import json

# Simple and Pythonic
client = MaphClient("http://localhost:8080")
sessions = client.create_store("sessions")

# Dict-like interface
data = {"session_id": "abc123", "user_id": 42}
sessions["session:abc123"] = json.dumps(data)

# Retrieve
value = json.loads(sessions["session:abc123"])
print(value)
```

See [python/README.md](python/README.md) for full documentation.

**Alternative: Raw requests library**

```python
import requests
import json

BASE_URL = "http://localhost:8080"

# Create a store
response = requests.post(f"{BASE_URL}/stores/sessions")
print(response.json())

# Set a value
data = {"session_id": "abc123", "user_id": 42}
response = requests.put(
    f"{BASE_URL}/stores/sessions/keys/session:abc123",
    data=json.dumps(data)
)
print(response.json())

# Get a value
response = requests.get(f"{BASE_URL}/stores/sessions/keys/session:abc123")
print(response.json()['value'])
```

### Using with JavaScript (Node.js)

```javascript
const axios = require('axios');

const BASE_URL = 'http://localhost:8080';

async function example() {
  // Create a store
  await axios.post(`${BASE_URL}/stores/cache`);

  // Set a value
  await axios.put(
    `${BASE_URL}/stores/cache/keys/config:theme`,
    'dark-mode'
  );

  // Get a value
  const response = await axios.get(
    `${BASE_URL}/stores/cache/keys/config:theme`
  );
  console.log(response.data.value);  // "dark-mode"
}

example();
```

---

## Running as a System Service (Daemon)

To run the maph server as a background daemon that starts automatically on boot, use systemd.

### Create a Systemd Service File

Create `/etc/systemd/system/maph-server.service`:

```ini
[Unit]
Description=maph v3 REST API Server
After=network.target
Documentation=https://github.com/queelius/maph

[Service]
Type=simple
User=maph
Group=maph
WorkingDirectory=/var/lib/maph
ExecStart=/usr/local/bin/maph_server_v3 8080
Restart=on-failure
RestartSec=5s

# Security hardening
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/var/lib/maph

# Resource limits
LimitNOFILE=65536
MemoryMax=4G

# Logging
StandardOutput=journal
StandardError=journal
SyslogIdentifier=maph-server

[Install]
WantedBy=multi-user.target
```

### Setup Instructions

```bash
# 1. Create dedicated user for the service
sudo useradd -r -s /bin/false maph

# 2. Create data directory
sudo mkdir -p /var/lib/maph/data
sudo chown -R maph:maph /var/lib/maph

# 3. Install the server binary
sudo cp integrations/rest_api/maph_server_v3 /usr/local/bin/
sudo chmod 755 /usr/local/bin/maph_server_v3

# 4. Copy the service file
sudo nano /etc/systemd/system/maph-server.service
# Paste the contents from above

# 5. Reload systemd configuration
sudo systemctl daemon-reload

# 6. Enable the service to start on boot
sudo systemctl enable maph-server

# 7. Start the service
sudo systemctl start maph-server

# 8. Check status
sudo systemctl status maph-server
```

### Service Management Commands

```bash
# Start the service
sudo systemctl start maph-server

# Stop the service
sudo systemctl stop maph-server

# Restart the service
sudo systemctl restart maph-server

# Check service status
sudo systemctl status maph-server

# View logs
sudo journalctl -u maph-server -f

# View logs from the last hour
sudo journalctl -u maph-server --since "1 hour ago"

# Disable auto-start on boot
sudo systemctl disable maph-server
```

### Monitoring the Service

```bash
# Real-time log monitoring
sudo journalctl -u maph-server -f

# Check if service is running
systemctl is-active maph-server

# Get detailed status
systemctl status maph-server

# View resource usage
systemctl show maph-server --property=MemoryCurrent,CPUUsageNSec
```

### Service Configuration Options

Edit `/etc/systemd/system/maph-server.service` to customize:

**Change Port:**
```ini
ExecStart=/usr/local/bin/maph_server_v3 9090
```

**Increase Memory Limit:**
```ini
MemoryMax=8G
```

**Add Environment Variables:**
```ini
Environment="MAPH_DATA_DIR=/var/lib/maph/data"
Environment="MAPH_LOG_LEVEL=debug"
```

**Change User/Group:**
```ini
User=www-data
Group=www-data
```

After any changes:
```bash
sudo systemctl daemon-reload
sudo systemctl restart maph-server
```

---

## Configuration

### Server Configuration

The server accepts one optional command-line argument:

```bash
./maph_server_v3 [port]
```

- **Default port:** 8080
- **Data directory:** `./data/` (created automatically)

### Store Configuration

Stores are created with default settings:
- **Slots:** 10,000 (default, can be customized in code)
- **Max probes:** 20 (linear probing limit)
- **Journal enabled:** Yes (for crash recovery)

To customize, modify `maph_server_v3.cpp`:

```cpp
maph::config cfg{slot_count{100000}};  // 100k slots instead of 10k
cfg.enable_journal = true;
cfg.max_probes = 50;  // More probing attempts
```

---

## Performance Tips

### 1. Adjust Slot Count

For optimal performance, provision 3× more slots than expected keys:

```cpp
// If expecting 1 million keys:
maph::config cfg{slot_count{3000000}};
```

### 2. Use Persistent Connections

Reuse HTTP connections to reduce overhead:

```bash
# Python with session
import requests
session = requests.Session()
for i in range(1000):
    session.put(f"{BASE_URL}/stores/test/keys/key_{i}", data=f"value_{i}")
```

### 3. Batch Operations

Group operations in shell scripts to amortize HTTP overhead:

```bash
# Good: Use parallel processing
seq 1 1000 | xargs -P 10 -I {} curl -X PUT -d "value_{}" \
  http://localhost:8080/stores/test/keys/key_{}
```

### 4. Monitor Resource Usage

```bash
# Check memory usage
ps aux | grep maph_server_v3

# Monitor with htop
htop -p $(pgrep maph_server_v3)
```

---

## Troubleshooting

### Server Won't Start

**Error: "Failed to start server on port 8080"**

**Solution:**
```bash
# Check if port is already in use
sudo lsof -i :8080

# Kill existing process
sudo kill $(sudo lsof -t -i:8080)

# Or use a different port
./maph_server_v3 9090
```

---

### Store Creation Fails

**Error: "Failed to create store"**

**Possible causes:**
1. Insufficient disk space
2. Permission denied on `data/` directory
3. Invalid store name

**Solution:**
```bash
# Check disk space
df -h .

# Verify permissions
ls -la data/

# Fix permissions
chmod 755 data/
```

---

### Key Not Found After Insert

**Issue:** PUT succeeds but GET returns 404

**Cause:** Hash collision or table full (load factor > 0.9)

**Solution:**
```bash
# Check load factor
curl http://localhost:8080/stores/mydb/stats

# If load_factor > 0.9, create a new store with more slots
curl -X POST http://localhost:8080/stores/mydb_v2
```

---

### High Latency

**Issue:** Response times > 1ms

**Possible causes:**
1. High load factor
2. Network latency
3. Large values
4. Disk I/O (if swapping)

**Diagnostics:**
```bash
# Check load factor
curl http://localhost:8080/stores/test/stats

# Monitor I/O
iostat -x 1

# Check memory
free -h
```

---

### Build Errors

**Error: "libmicrohttpd not found"**

**Solution:**
```bash
sudo apt-get install libmicrohttpd-dev
```

**Error: "std::expected not found"**

**Cause:** C++23 not supported

**Solution:**
```bash
# Use GCC 13+ or Clang 16+
g++ --version

# Update if needed
sudo apt-get install g++-13
export CXX=g++-13
```

---

## Security Considerations

### 1. Network Access

**Production deployments should:**
- Run behind a reverse proxy (nginx, Apache)
- Use TLS/HTTPS for encryption
- Implement authentication/authorization

**Example nginx configuration:**

```nginx
server {
    listen 443 ssl;
    server_name maph.example.com;

    ssl_certificate /path/to/cert.pem;
    ssl_certificate_key /path/to/key.pem;

    location / {
        proxy_pass http://localhost:8080;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;

        # Authentication
        auth_basic "maph API";
        auth_basic_user_file /etc/nginx/.htpasswd;
    }
}
```

### 2. Resource Limits

Set limits in systemd service file:
```ini
MemoryMax=4G
LimitNOFILE=65536
CPUQuota=200%
```

### 3. Firewall Rules

```bash
# Allow only from specific IPs
sudo ufw allow from 192.168.1.0/24 to any port 8080

# Or use iptables
sudo iptables -A INPUT -p tcp -s 192.168.1.0/24 --dport 8080 -j ACCEPT
```

---

## License

maph v3 is released under the MIT License. See LICENSE file for details.

---

## Support

- **Issues:** https://github.com/queelius/maph/issues
- **Documentation:** https://github.com/queelius/maph/docs
- **Benchmarks:** See `docs/BENCHMARK_RESULTS.md`

---

## Version

**maph v3.0.0** - REST API Server

Built with:
- C++23
- libmicrohttpd
- std::expected for error handling
- Memory-mapped I/O for persistence
