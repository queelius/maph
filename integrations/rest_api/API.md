# Maph REST API Documentation

## Overview

The Maph REST API provides HTTP-based access to Maph databases, enabling integration with web applications, microservices, and any HTTP-capable client. The API follows RESTful principles and returns JSON responses.

## Quick Start

### Starting the Server

```bash
# Start server on default port (8080)
./maph_server data.maph

# Start on custom port
./maph_server data.maph --port 3000

# Start with specific database
./maph_server /path/to/database.maph --port 8080

# Enable CORS for web clients
./maph_server data.maph --cors

# Verbose logging
./maph_server data.maph --verbose
```

### Basic Usage

```bash
# Set a value
curl -X PUT http://localhost:8080/api/kv/user:1 \
  -H "Content-Type: application/json" \
  -d '{"name": "Alice", "age": 30}'

# Get a value
curl http://localhost:8080/api/kv/user:1

# Delete a value
curl -X DELETE http://localhost:8080/api/kv/user:1

# Get statistics
curl http://localhost:8080/api/stats
```

## API Reference

### Base URL

```
http://localhost:8080
```

All API endpoints are prefixed with `/api`.

### Authentication

The current version does not require authentication. Future versions will support:
- API keys
- JWT tokens
- Basic authentication

### Content Types

- **Request**: `application/json` for body data
- **Response**: `application/json`

### Common Response Codes

| Code | Meaning |
|------|---------|
| 200 | Success |
| 201 | Created |
| 204 | No Content (successful delete) |
| 400 | Bad Request |
| 404 | Not Found |
| 413 | Payload Too Large |
| 500 | Internal Server Error |
| 503 | Service Unavailable |

## Endpoints

### Key-Value Operations

#### GET /api/kv/{key}

Retrieve a value by key.

**Parameters:**
- `key` (path) - The key to retrieve

**Response:**
- 200: Value found
- 404: Key not found

**Example Request:**
```bash
curl http://localhost:8080/api/kv/user:123
```

**Example Response:**
```json
{
  "key": "user:123",
  "value": {
    "name": "Alice",
    "email": "alice@example.com",
    "created": "2024-01-15T10:30:00Z"
  },
  "size": 89,
  "hash": "0x1a2b3c4d"
}
```

#### PUT /api/kv/{key}

Store or update a value.

**Parameters:**
- `key` (path) - The key to store
- Body - JSON value to store

**Response:**
- 201: Created (new key)
- 200: Updated (existing key)
- 413: Value too large (>496 bytes)
- 503: Database full

**Example Request:**
```bash
curl -X PUT http://localhost:8080/api/kv/config:app \
  -H "Content-Type: application/json" \
  -d '{
    "version": "1.2.3",
    "features": ["auth", "cache"],
    "timeout": 30
  }'
```

**Example Response:**
```json
{
  "status": "created",
  "key": "config:app",
  "size": 67
}
```

#### DELETE /api/kv/{key}

Remove a key-value pair.

**Parameters:**
- `key` (path) - The key to delete

**Response:**
- 204: Successfully deleted
- 404: Key not found

**Example Request:**
```bash
curl -X DELETE http://localhost:8080/api/kv/temp:session
```

#### HEAD /api/kv/{key}

Check if a key exists without retrieving the value.

**Parameters:**
- `key` (path) - The key to check

**Response Headers:**
- `X-Exists`: "true" or "false"
- `X-Size`: Size in bytes (if exists)

**Example Request:**
```bash
curl -I http://localhost:8080/api/kv/user:123
```

### Batch Operations

#### POST /api/batch/get

Retrieve multiple keys in one request.

**Request Body:**
```json
{
  "keys": ["key1", "key2", "key3"]
}
```

**Response:**
```json
{
  "results": {
    "key1": {
      "found": true,
      "value": {"data": "value1"}
    },
    "key2": {
      "found": false
    },
    "key3": {
      "found": true,
      "value": {"data": "value3"}
    }
  },
  "found": 2,
  "missing": 1
}
```

**Example Request:**
```bash
curl -X POST http://localhost:8080/api/batch/get \
  -H "Content-Type: application/json" \
  -d '{"keys": ["user:1", "user:2", "user:3"]}'
```

#### POST /api/batch/set

Store multiple key-value pairs.

**Request Body:**
```json
{
  "operations": [
    {"key": "key1", "value": {"data": "value1"}},
    {"key": "key2", "value": {"data": "value2"}}
  ]
}
```

**Response:**
```json
{
  "stored": 2,
  "failed": 0,
  "results": [
    {"key": "key1", "status": "created"},
    {"key": "key2", "status": "updated"}
  ]
}
```

**Example Request:**
```bash
curl -X POST http://localhost:8080/api/batch/set \
  -H "Content-Type: application/json" \
  -d '{
    "operations": [
      {"key": "product:1", "value": {"name": "Widget", "price": 29.99}},
      {"key": "product:2", "value": {"name": "Gadget", "price": 49.99}}
    ]
  }'
```

#### POST /api/batch/delete

Delete multiple keys.

**Request Body:**
```json
{
  "keys": ["key1", "key2", "key3"]
}
```

**Response:**
```json
{
  "deleted": 2,
  "not_found": 1,
  "results": {
    "key1": "deleted",
    "key2": "deleted",
    "key3": "not_found"
  }
}
```

### Query Operations

#### GET /api/scan

Scan all keys in the database.

**Query Parameters:**
- `limit` (optional) - Maximum number of results (default: 100, max: 10000)
- `offset` (optional) - Starting position (default: 0)
- `prefix` (optional) - Filter by key prefix

**Response:**
```json
{
  "keys": [
    {"key": "user:1", "size": 45, "hash": "0x1234"},
    {"key": "user:2", "size": 67, "hash": "0x5678"}
  ],
  "total": 2,
  "limit": 100,
  "offset": 0,
  "has_more": false
}
```

**Example Request:**
```bash
# Get first 50 keys
curl "http://localhost:8080/api/scan?limit=50"

# Get keys with prefix
curl "http://localhost:8080/api/scan?prefix=user:"

# Paginate through results
curl "http://localhost:8080/api/scan?limit=100&offset=200"
```

#### POST /api/search

Search for values matching criteria (requires scanning).

**Request Body:**
```json
{
  "field": "name",
  "operator": "contains",
  "value": "Alice",
  "limit": 10
}
```

**Response:**
```json
{
  "matches": [
    {
      "key": "user:1",
      "value": {"name": "Alice Smith", "age": 30}
    }
  ],
  "scanned": 1000,
  "matched": 1
}
```

**Note:** This operation scans all values and can be slow for large databases.

### Database Operations

#### GET /api/stats

Get database statistics.

**Response:**
```json
{
  "database": {
    "path": "/var/lib/maph/data.maph",
    "size_bytes": 512512000,
    "size_mb": 488.77
  },
  "slots": {
    "total": 1000000,
    "used": 42531,
    "free": 957469,
    "static": 800000,
    "dynamic": 200000
  },
  "performance": {
    "load_factor": 0.0425,
    "generation": 85062,
    "avg_lookup_ns": 198,
    "avg_insert_ns": 452
  },
  "server": {
    "uptime_seconds": 3600,
    "total_requests": 1234567,
    "active_connections": 42,
    "memory_usage_mb": 512.3
  }
}
```

#### POST /api/compact

Trigger database compaction (reorganize for better performance).

**Response:**
```json
{
  "status": "completed",
  "before": {
    "fragmentation": 0.15,
    "load_factor": 0.75
  },
  "after": {
    "fragmentation": 0.02,
    "load_factor": 0.65
  },
  "duration_ms": 1234
}
```

#### POST /api/sync

Force synchronization to disk.

**Response:**
```json
{
  "status": "synced",
  "timestamp": "2024-01-15T10:30:00Z"
}
```

#### GET /api/info

Get server information.

**Response:**
```json
{
  "version": "1.0.0",
  "database": "data.maph",
  "features": ["batch", "scan", "stats"],
  "limits": {
    "max_value_size": 496,
    "max_batch_size": 1000,
    "max_scan_limit": 10000
  }
}
```

### Import/Export

#### POST /api/import

Import data from JSONL format.

**Request Body (multipart/form-data):**
- `file` - JSONL file to import
- `mode` - "overwrite" or "merge" (default: "merge")

**Response:**
```json
{
  "imported": 10000,
  "failed": 2,
  "duration_ms": 234,
  "errors": [
    {"line": 456, "error": "value too large"},
    {"line": 789, "error": "invalid JSON"}
  ]
}
```

**Example Request:**
```bash
curl -X POST http://localhost:8080/api/import \
  -F "file=@data.jsonl" \
  -F "mode=merge"
```

#### GET /api/export

Export database to JSONL format.

**Query Parameters:**
- `prefix` (optional) - Export only keys with this prefix

**Response:**
Streams JSONL data with one key-value pair per line.

**Example Request:**
```bash
# Export entire database
curl http://localhost:8080/api/export > backup.jsonl

# Export specific prefix
curl "http://localhost:8080/api/export?prefix=user:" > users.jsonl
```

## Web Interface

The server includes a built-in web interface accessible at:

```
http://localhost:8080/
```

Features:
- Visual key-value browser
- Statistics dashboard
- Query builder
- Import/export tools
- Real-time performance metrics

## WebSocket API

Connect to real-time updates via WebSocket:

```javascript
const ws = new WebSocket('ws://localhost:8080/ws');

// Subscribe to changes
ws.send(JSON.stringify({
  action: 'subscribe',
  prefix: 'user:'
}));

// Receive updates
ws.onmessage = (event) => {
  const data = JSON.parse(event.data);
  console.log('Change:', data);
  // {type: 'set', key: 'user:123', value: {...}}
};
```

## Client Libraries

### JavaScript/Node.js

```javascript
const MaphClient = require('maph-client');

const client = new MaphClient({
  host: 'localhost',
  port: 8080
});

// Set value
await client.set('user:1', {name: 'Alice'});

// Get value
const value = await client.get('user:1');

// Batch operations
const results = await client.mget(['user:1', 'user:2']);
```

### Python

```python
from maph_client import MaphClient

client = MaphClient(host='localhost', port=8080)

# Set value
client.set('user:1', {'name': 'Alice'})

# Get value
value = client.get('user:1')

# Batch operations
results = client.mget(['user:1', 'user:2'])
```

### Go

```go
import "github.com/maph/maph-go"

client := maph.NewClient("localhost:8080")

// Set value
client.Set("user:1", map[string]interface{}{
    "name": "Alice",
})

// Get value
value, err := client.Get("user:1")
```

## Performance Considerations

### Connection Pooling

The server supports HTTP keep-alive. Reuse connections for better performance:

```bash
curl -H "Connection: keep-alive" http://localhost:8080/api/kv/key
```

### Batch vs Individual Operations

For multiple operations, use batch endpoints:

```
Individual: 1000 requests × 1ms = 1000ms
Batch: 1 request × 10ms = 10ms
```

### Compression

The server supports gzip compression:

```bash
curl -H "Accept-Encoding: gzip" http://localhost:8080/api/stats
```

### Rate Limiting

Default limits (configurable):
- 10,000 requests/second per IP
- 1,000 concurrent connections
- 10MB max request body

## Error Handling

### Error Response Format

```json
{
  "error": {
    "code": "VALUE_TOO_LARGE",
    "message": "Value exceeds maximum size of 496 bytes",
    "details": {
      "provided_size": 512,
      "max_size": 496
    }
  }
}
```

### Error Codes

| Code | Description |
|------|-------------|
| KEY_NOT_FOUND | Key does not exist |
| VALUE_TOO_LARGE | Value exceeds 496 bytes |
| DATABASE_FULL | No space for new keys |
| INVALID_JSON | Malformed JSON in request |
| BATCH_TOO_LARGE | Batch exceeds limit |
| SCAN_LIMIT_EXCEEDED | Scan limit too high |
| INTERNAL_ERROR | Server error |

## Security

### Best Practices

1. **Network Security**:
   - Run behind reverse proxy (nginx, HAProxy)
   - Use HTTPS in production
   - Implement firewall rules

2. **Access Control**:
   - Future: API key authentication
   - Future: Role-based access control
   - Future: IP whitelisting

3. **Input Validation**:
   - Key length limits
   - Value size validation
   - JSON schema validation

### CORS Configuration

```bash
# Enable CORS for specific origin
./maph_server data.maph --cors-origin "https://example.com"

# Enable CORS for all origins (development only)
./maph_server data.maph --cors-origin "*"
```

## Monitoring

### Health Check

```bash
curl http://localhost:8080/api/health
```

Response:
```json
{
  "status": "healthy",
  "database": "connected",
  "memory": "ok",
  "disk": "ok"
}
```

### Metrics Endpoint

```bash
curl http://localhost:8080/metrics
```

Returns Prometheus-compatible metrics:

```
# HELP maph_requests_total Total number of requests
# TYPE maph_requests_total counter
maph_requests_total{method="GET"} 12345

# HELP maph_response_time_seconds Response time in seconds
# TYPE maph_response_time_seconds histogram
maph_response_time_seconds_bucket{le="0.001"} 10000
```

## Configuration

### Environment Variables

```bash
MAPH_PORT=8080                 # Server port
MAPH_HOST=0.0.0.0             # Bind address
MAPH_MAX_CONNECTIONS=1000      # Max concurrent connections
MAPH_REQUEST_TIMEOUT=30        # Request timeout in seconds
MAPH_CORS_ENABLED=true         # Enable CORS
MAPH_GZIP_ENABLED=true         # Enable compression
MAPH_LOG_LEVEL=info           # Log level (debug|info|warn|error)
```

### Configuration File

```yaml
# maph_server.yaml
server:
  port: 8080
  host: 0.0.0.0
  max_connections: 1000

database:
  path: /var/lib/maph/data.maph
  sync_interval: 60  # seconds
  
security:
  cors_enabled: true
  cors_origins:
    - https://app.example.com
  rate_limit:
    requests_per_second: 10000
    
logging:
  level: info
  file: /var/log/maph/server.log
```

## Docker Deployment

### Dockerfile

```dockerfile
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y build-essential cmake
COPY . /app
WORKDIR /app
RUN mkdir build && cd build && cmake .. && make maph_server
RUN cp build/maph_server /usr/local/bin/

EXPOSE 8080
VOLUME ["/data"]
CMD ["maph_server", "/data/database.maph"]
```

### Docker Compose

```yaml
version: '3'
services:
  maph:
    build: .
    ports:
      - "8080:8080"
    volumes:
      - maph_data:/data
    environment:
      - MAPH_PORT=8080
      - MAPH_LOG_LEVEL=info
    restart: unless-stopped

volumes:
  maph_data:
```

### Kubernetes Deployment

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: maph-server
spec:
  replicas: 3
  selector:
    matchLabels:
      app: maph
  template:
    metadata:
      labels:
        app: maph
    spec:
      containers:
      - name: maph
        image: maph:latest
        ports:
        - containerPort: 8080
        volumeMounts:
        - name: data
          mountPath: /data
      volumes:
      - name: data
        persistentVolumeClaim:
          claimName: maph-pvc
```

## Examples

### Session Store

```javascript
// Store session
await fetch('http://localhost:8080/api/kv/session:abc123', {
  method: 'PUT',
  headers: {'Content-Type': 'application/json'},
  body: JSON.stringify({
    user_id: 42,
    expires: '2024-01-16T10:00:00Z',
    data: {cart: ['item1', 'item2']}
  })
});

// Get session
const response = await fetch('http://localhost:8080/api/kv/session:abc123');
const session = await response.json();
```

### Cache Layer

```python
import requests

class MaphCache:
    def __init__(self, host='localhost:8080'):
        self.base_url = f'http://{host}/api'
    
    def get(self, key):
        r = requests.get(f'{self.base_url}/kv/{key}')
        if r.status_code == 200:
            return r.json()['value']
        return None
    
    def set(self, key, value, ttl=None):
        # Note: TTL not natively supported, store expiry in value
        if ttl:
            value['_expires'] = time.time() + ttl
        r = requests.put(f'{self.base_url}/kv/{key}', json=value)
        return r.status_code in (200, 201)
```

### Real-time Dashboard

```html
<!DOCTYPE html>
<html>
<head>
    <title>Maph Dashboard</title>
</head>
<body>
    <div id="stats"></div>
    <script>
        // Poll stats every second
        setInterval(async () => {
            const res = await fetch('/api/stats');
            const stats = await res.json();
            document.getElementById('stats').innerHTML = `
                <h2>Database Stats</h2>
                <p>Used Slots: ${stats.slots.used}</p>
                <p>Load Factor: ${(stats.performance.load_factor * 100).toFixed(2)}%</p>
                <p>Requests: ${stats.server.total_requests}</p>
            `;
        }, 1000);
    </script>
</body>
</html>
```

## Troubleshooting

### Connection Refused

```bash
# Check if server is running
ps aux | grep maph_server

# Check port availability
netstat -tlnp | grep 8080

# Check firewall
ufw status
```

### Slow Performance

```bash
# Check database stats
curl http://localhost:8080/api/stats

# Monitor server metrics
curl http://localhost:8080/metrics

# Enable debug logging
MAPH_LOG_LEVEL=debug ./maph_server data.maph
```

### Data Corruption

```bash
# Verify database integrity
maph stats data.maph

# Create backup
cp data.maph data.maph.backup

# Rebuild if necessary
maph export data.maph > backup.jsonl
maph create newdata.maph 1000000
maph import newdata.maph < backup.jsonl
```