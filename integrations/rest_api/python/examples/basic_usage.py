#!/usr/bin/env python3
"""
Basic usage example for maph v3 Python client

Demonstrates creating stores, inserting data, and querying.
"""

from maph_client import MaphClient, StoreExistsError

# Connect to maph server
client = MaphClient("http://localhost:8080")

# Create a new store (or use existing)
try:
    store = client.create_store("users")
    print("✓ Created store 'users'")
except StoreExistsError:
    store = client.get_store("users")
    print("✓ Using existing store 'users'")

# Insert some data
print("\nInserting user data...")
store["user:1001"] = "Alice Johnson"
store["user:1002"] = "Bob Smith"
store["user:1003"] = "Charlie Davis"
print("✓ Inserted 3 users")

# Query data
print("\nQuerying users...")
print(f"user:1001 -> {store['user:1001']}")
print(f"user:1002 -> {store['user:1002']}")
print(f"user:1003 -> {store['user:1003']}")

# Check if key exists
print("\nChecking existence...")
print(f"'user:1001' exists: {'user:1001' in store}")
print(f"'user:9999' exists: {'user:9999' in store}")

# Get with default
print("\nGet with default...")
print(f"user:9999 (with default): {store.get('user:9999', 'NOT FOUND')}")

# Delete a key
print("\nDeleting user:1002...")
del store["user:1002"]
print(f"'user:1002' exists: {'user:1002' in store}")

# Get store statistics
print("\nStore statistics...")
stats = store.stats()
print(f"Size: {stats.size} keys")
print(f"Load factor: {stats.load_factor:.3f}")

# List all stores
print("\nAll stores:")
for info in client.list_stores():
    print(f"  {info.name}: {info.size} keys, {info.load_factor:.1%} full")

print("\n✓ Demo complete!")
