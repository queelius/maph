#!/usr/bin/env python3
"""
Perfect hash optimization example for maph v3 Python client

Demonstrates optimizing a store for guaranteed O(1) lookups.
"""

from maph_client import MaphClient, StoreExistsError
import time

# Connect to server
client = MaphClient("http://localhost:8080")

# Create store for configuration settings
try:
    config = client.create_store("app_config")
    print("✓ Created store 'app_config'")
except StoreExistsError:
    config = client.get_store("app_config")
    print("✓ Using existing store 'app_config'")

# Insert known, static configuration keys
print("\nInserting configuration settings...")
settings = {
    "app.name": "MyApplication",
    "app.version": "1.0.0",
    "app.environment": "production",
    "database.host": "localhost",
    "database.port": "5432",
    "database.name": "myapp_db",
    "cache.ttl": "3600",
    "cache.max_size": "10000",
    "api.timeout": "30",
    "api.max_retries": "3",
}

for key, value in settings.items():
    config[key] = value
    print(f"  {key} = {value}")

print(f"\n✓ Inserted {len(settings)} configuration settings")

# Get stats before optimization
stats_before = config.stats()
print(f"\nBefore optimization:")
print(f"  Size: {stats_before.size} keys")
print(f"  Load factor: {stats_before.load_factor:.3f}")

# Optimize to perfect hash
print("\nOptimizing to perfect hash...")
start = time.time()
config.optimize()
elapsed = time.time() - start
print(f"✓ Optimization complete in {elapsed*1000:.1f}ms")

# Get stats after optimization
stats_after = config.stats()
print(f"\nAfter optimization:")
print(f"  Size: {stats_after.size} keys")
print(f"  Load factor: {stats_after.load_factor:.3f}")

# Verify all keys are still accessible
print("\nVerifying all keys after optimization...")
for key in settings.keys():
    value = config[key]
    assert value == settings[key], f"Mismatch for {key}"
print("✓ All keys verified - guaranteed O(1) lookups!")

# Demonstrate that new keys still work (using fallback hash)
print("\nAdding new key after optimization...")
config["app.debug"] = "false"
print("✓ New keys still work (using fallback hash)")

# Performance note
print("\n" + "="*60)
print("PERFECT HASH BENEFITS:")
print("="*60)
print("✓ Existing keys: O(1) guaranteed, zero collisions")
print("✓ No hash table resize needed")
print("✓ Optimal memory layout")
print("✓ New keys: Still work with fallback hash")
print("\nBest for: Static/mostly-static key sets (configs, lookups, etc.)")
print("="*60)

print("\n✓ Perfect hash optimization demo complete!")
