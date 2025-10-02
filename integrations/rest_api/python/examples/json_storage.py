#!/usr/bin/env python3
"""
JSON storage example for maph v3 Python client

Demonstrates storing and retrieving JSON documents.
"""

import json
from maph_client import MaphClient, StoreExistsError

# Connect to server
client = MaphClient("http://localhost:8080")

# Create store for user profiles
try:
    profiles = client.create_store("profiles")
    print("✓ Created store 'profiles'")
except StoreExistsError:
    profiles = client.get_store("profiles")
    print("✓ Using existing store 'profiles'")

# Store JSON user profiles
print("\nStoring user profiles...")

users = [
    {
        "id": 1001,
        "name": "Alice Johnson",
        "email": "alice@example.com",
        "roles": ["admin", "developer"],
        "active": True
    },
    {
        "id": 1002,
        "name": "Bob Smith",
        "email": "bob@example.com",
        "roles": ["developer"],
        "active": True
    },
    {
        "id": 1003,
        "name": "Charlie Davis",
        "email": "charlie@example.com",
        "roles": ["viewer"],
        "active": False
    }
]

for user in users:
    key = f"user:{user['id']}"
    value = json.dumps(user)
    profiles[key] = value
    print(f"  Stored {key}")

# Retrieve and parse JSON
print("\nRetrieving profiles...")
for user_id in [1001, 1002, 1003]:
    key = f"user:{user_id}"
    value = profiles[key]
    user = json.loads(value)

    status = "active" if user["active"] else "inactive"
    roles = ", ".join(user["roles"])
    print(f"  {user['name']} ({user['email']}) - {status} - roles: {roles}")

# Update a profile
print("\nUpdating user:1002...")
user_data = json.loads(profiles["user:1002"])
user_data["roles"].append("admin")
user_data["updated"] = "2025-10-01"
profiles["user:1002"] = json.dumps(user_data)
print(f"  Updated roles: {user_data['roles']}")

# Store statistics
print("\nStore statistics...")
stats = profiles.stats()
print(f"  Total profiles: {stats.size}")
print(f"  Load factor: {stats.load_factor:.1%}")

print("\n✓ JSON demo complete!")
