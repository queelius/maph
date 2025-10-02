#!/usr/bin/env python3
"""
Quick test script for maph Python client

Run with maph server running on localhost:8080
"""

import sys
import os

# Add parent directory to path to import maph_client
sys.path.insert(0, os.path.dirname(__file__))

from maph_client import MaphClient, StoreExistsError, KeyNotFoundError

def test_client():
    """Run basic tests on the client"""
    print("Testing maph Python client...")

    # Connect
    client = MaphClient("http://localhost:8080")
    print("✓ Connected to server")

    # Create or get store
    try:
        test_store = client.create_store("test_py")
        print("✓ Created store 'test_py'")
    except StoreExistsError:
        test_store = client.get_store("test_py")
        print("✓ Using existing store 'test_py'")

    # Insert
    test_store["key1"] = "value1"
    test_store["key2"] = "value2"
    print("✓ Inserted 2 keys")

    # Read
    assert test_store["key1"] == "value1"
    assert test_store["key2"] == "value2"
    print("✓ Read keys successfully")

    # Contains
    assert "key1" in test_store
    assert "key999" not in test_store
    print("✓ Contains check works")

    # Get with default
    assert test_store.get("key999", "default") == "default"
    print("✓ Get with default works")

    # Delete
    del test_store["key1"]
    assert "key1" not in test_store
    print("✓ Delete works")

    # Stats
    stats = test_store.stats()
    print(f"✓ Stats: {stats.size} keys, {stats.load_factor:.3f} load factor")

    # List stores
    stores = client.list_stores()
    assert any(s.name == "test_py" for s in stores)
    print(f"✓ List stores works ({len(stores)} stores)")

    # Error handling
    try:
        _ = test_store["nonexistent"]
        assert False, "Should have raised KeyNotFoundError"
    except KeyNotFoundError:
        print("✓ KeyNotFoundError raised correctly")

    print("\n✅ All tests passed!")
    return True

if __name__ == "__main__":
    try:
        test_client()
    except Exception as e:
        print(f"\n❌ Test failed: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
