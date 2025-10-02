#!/usr/bin/env python3
"""
Batch operations example for maph v3 Python client

Demonstrates efficient bulk inserts and queries with connection pooling.
"""

import time
from maph_client import MaphClient, StoreExistsError

# Connect with custom timeout
client = MaphClient("http://localhost:8080", timeout=30.0)

# Create store for cache data
try:
    cache = client.create_store("cache")
    print("✓ Created store 'cache'")
except StoreExistsError:
    cache = client.get_store("cache")
    print("✓ Using existing store 'cache'")

# Batch insert
print("\nBatch inserting 1000 keys...")
start = time.time()

for i in range(1000):
    key = f"item:{i:04d}"
    value = f"value_{i}"
    cache[key] = value

    if (i + 1) % 100 == 0:
        print(f"  Inserted {i + 1} keys...")

elapsed = time.time() - start
print(f"✓ Inserted 1000 keys in {elapsed:.2f}s ({1000/elapsed:.0f} ops/sec)")

# Batch query
print("\nBatch querying 100 random keys...")
start = time.time()

import random
keys_to_query = [f"item:{random.randint(0, 999):04d}" for _ in range(100)]
values = [cache[key] for key in keys_to_query]

elapsed = time.time() - start
print(f"✓ Queried 100 keys in {elapsed:.2f}s ({100/elapsed:.0f} ops/sec)")

# Check final statistics
print("\nFinal statistics...")
stats = cache.stats()
print(f"  Total keys: {stats.size}")
print(f"  Load factor: {stats.load_factor:.1%}")

print("\n✓ Batch operations complete!")
