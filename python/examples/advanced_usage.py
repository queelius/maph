#!/usr/bin/env python3
"""Advanced examples showcasing the flexibility of approximate_filters Python bindings."""

import approximate_filters as af
import random
import time
from typing import List, Dict, Any

def demo_storage_size_comparison():
    """Compare different storage sizes for the same dataset."""
    print("=== Storage Size Comparison ===\n")
    
    # Generate test data
    elements = [f"user_{i:06d}" for i in range(10000)]
    test_set = random.sample(elements, 100) + [f"fake_{i}" for i in range(100)]
    
    # Create filters with different storage sizes
    filters = {
        8: af.create_filter(elements, bits=8),
        16: af.create_filter(elements, bits=16),
        32: af.create_filter(elements, bits=32),
        64: af.create_filter(elements, bits=64)
    }
    
    # Compare storage and accuracy
    for bits, filter_obj in filters.items():
        false_positives = sum(1 for x in test_set[100:] if x in filter_obj)
        print(f"{bits}-bit filter:")
        print(f"  Storage: {filter_obj.storage_bytes():,} bytes")
        print(f"  Theoretical FPR: {filter_obj.fpr:.6%}")
        print(f"  Observed FP: {false_positives}/100 = {false_positives/100:.2%}")
        print()

def demo_threshold_tuning():
    """Demonstrate dynamic FPR tuning with threshold filters."""
    print("=== Threshold Filter Tuning ===\n")
    
    # IP allowlist scenario
    allowed_ips = [f"192.168.1.{i}" for i in range(1, 51)]
    
    # Create builder
    builder = af.PerfectHashBuilder(error_rate=0.0)
    map_builder = af.ApproxMapBuilder(builder)
    
    # Create filters with different target FPRs
    target_fprs = [0.5, 0.1, 0.05, 0.01, 0.001]
    
    for target_fpr in target_fprs:
        # Use 16-bit storage for balance
        filter_obj = af.create_threshold_filter(allowed_ips, target_fpr, bits=16)
        
        # Test with random IPs
        test_ips = [f"10.0.0.{i}" for i in range(1, 1001)]
        false_positives = sum(1 for ip in test_ips if ip in filter_obj)
        
        print(f"Target FPR: {target_fpr:.3f}")
        print(f"  Observed FPR: {false_positives/1000:.3f}")
        print(f"  Storage: {filter_obj.storage_bytes()} bytes")
        print()

def demo_compact_lookup_tables():
    """Showcase compact lookup tables for various data types."""
    print("=== Compact Lookup Tables ===\n")
    
    # Example 1: HTTP status codes
    status_codes = {
        "/api/users": 200,
        "/api/login": 200,
        "/api/admin": 403,
        "/api/deleted": 404,
        "/api/error": 500,
    }
    
    lookup = af.create_lookup(
        list(status_codes.keys()),
        list(status_codes.values()),
        bits=16  # 16-bit sufficient for status codes
    )
    
    print("HTTP Status Lookup:")
    for endpoint in status_codes:
        print(f"  {endpoint}: {lookup[endpoint]}")
    print(f"  Storage: {lookup.storage_bytes()} bytes")
    print(f"  (vs dict: ~{len(str(status_codes))} bytes)")
    print()
    
    # Example 2: User permission levels
    permissions = {
        "alice": 3,     # admin
        "bob": 2,       # moderator
        "charlie": 1,   # user
        "david": 1,     # user
        "eve": 0,       # guest
    }
    
    perm_lookup = af.create_lookup(
        list(permissions.keys()),
        list(permissions.values()),
        bits=8  # Only need 8 bits for permission levels
    )
    
    print("Permission Levels:")
    for user, level in permissions.items():
        retrieved = perm_lookup.get(user, -1)
        role = ["guest", "user", "moderator", "admin"][retrieved] if retrieved >= 0 else "unknown"
        print(f"  {user}: level {retrieved} ({role})")
    print()

def demo_builder_pattern_flexibility():
    """Demonstrate the flexibility of the builder pattern."""
    print("=== Builder Pattern Flexibility ===\n")
    
    # Create a dataset
    dataset = [f"item_{i:04d}" for i in range(1000)]
    
    # Create builders with different configurations
    configs = [
        {"error_rate": 0.0, "load_factor": 1.0, "label": "Perfect hash, tight packing"},
        {"error_rate": 0.01, "load_factor": 1.5, "label": "1% error, relaxed packing"},
        {"error_rate": 0.05, "load_factor": 2.0, "label": "5% error, loose packing"},
    ]
    
    for config in configs:
        # Build with configuration
        builder = af.PerfectHashBuilder(error_rate=config["error_rate"])
        map_builder = af.ApproxMapBuilder(builder).with_load_factor(config["load_factor"])
        
        # Create 32-bit filter
        filter_obj = map_builder.build_filter_32bit(dataset)
        
        print(f"{config['label']}:")
        print(f"  Storage: {filter_obj.storage_bytes()} bytes")
        print(f"  FNR: {filter_obj.false_negative_rate():.2%}")
        print(f"  Elements: {len(dataset)}")
        print()

def demo_performance_characteristics():
    """Measure performance characteristics of different configurations."""
    print("=== Performance Characteristics ===\n")
    
    # Generate large dataset
    n = 100000
    elements = [f"key_{i:08d}" for i in range(n)]
    queries = random.sample(elements, 1000) + [f"miss_{i}" for i in range(1000)]
    
    configurations = [
        (8, "Ultra-compact (8-bit)"),
        (16, "Balanced (16-bit)"),
        (32, "High-accuracy (32-bit)"),
    ]
    
    for bits, label in configurations:
        # Build filter
        start = time.perf_counter()
        filter_obj = af.create_filter(elements, bits=bits)
        build_time = time.perf_counter() - start
        
        # Measure query time
        start = time.perf_counter()
        results = [x in filter_obj for x in queries]
        query_time = time.perf_counter() - start
        
        print(f"{label}:")
        print(f"  Build time: {build_time:.3f}s for {n:,} elements")
        print(f"  Query time: {query_time*1000:.3f}ms for 2,000 queries")
        print(f"  Throughput: {2000/query_time:,.0f} queries/sec")
        print(f"  Space: {filter_obj.storage_bytes()/n:.2f} bytes/element")
        print()

def demo_composite_filters():
    """Create composite filtering strategies."""
    print("=== Composite Filtering Strategies ===\n")
    
    # Multi-tier access control
    vip_users = ["alice", "bob", "charlie"]
    regular_users = [f"user_{i:03d}" for i in range(100)]
    
    # Create two-tier system: VIP with high accuracy, regular with lower
    vip_filter = af.create_filter(vip_users, bits=32)  # High accuracy for VIP
    regular_filter = af.create_filter(regular_users, bits=8)  # Space-efficient for regular
    
    def check_access(username: str) -> str:
        if username in vip_filter:
            return "VIP Access"
        elif username in regular_filter:
            return "Regular Access"
        else:
            return "No Access"
    
    # Test the system
    test_users = ["alice", "user_050", "hacker", "bob", "user_999"]
    
    print("Multi-tier Access Control:")
    print(f"  VIP storage: {vip_filter.storage_bytes()} bytes")
    print(f"  Regular storage: {regular_filter.storage_bytes()} bytes")
    print(f"  Total: {vip_filter.storage_bytes() + regular_filter.storage_bytes()} bytes")
    print("\nAccess checks:")
    for user in test_users:
        print(f"  {user}: {check_access(user)}")
    print()

def demo_data_deduplication():
    """Use filters for efficient data deduplication."""
    print("=== Data Deduplication ===\n")
    
    # Simulate streaming data with duplicates
    stream = []
    for i in range(1000):
        if random.random() < 0.3:  # 30% chance of duplicate
            stream.append(f"msg_{random.randint(0, 100):03d}")
        else:
            stream.append(f"msg_{i:03d}")
    
    # Build filter from first batch
    initial_batch = stream[:500]
    seen_filter = af.create_filter(initial_batch, bits=16)
    
    # Process remaining stream
    new_messages = []
    likely_duplicates = []
    
    for msg in stream[500:]:
        if msg in seen_filter:
            likely_duplicates.append(msg)
        else:
            new_messages.append(msg)
    
    # Calculate actual duplicates
    actual_dups = sum(1 for msg in stream[500:] if msg in initial_batch)
    
    print(f"Stream processing results:")
    print(f"  Initial batch: {len(initial_batch)} messages")
    print(f"  Processed: {len(stream[500:])} messages")
    print(f"  Likely duplicates: {len(likely_duplicates)}")
    print(f"  Actual duplicates: {actual_dups}")
    print(f"  False positives: {len(likely_duplicates) - actual_dups}")
    print(f"  Filter size: {seen_filter.storage_bytes()} bytes")
    print(f"  Memory saved vs set: ~{len(initial_batch) * 50 - seen_filter.storage_bytes()} bytes")

if __name__ == "__main__":
    demos = [
        demo_storage_size_comparison,
        demo_threshold_tuning,
        demo_compact_lookup_tables,
        demo_builder_pattern_flexibility,
        demo_performance_characteristics,
        demo_composite_filters,
        demo_data_deduplication,
    ]
    
    for demo in demos:
        demo()
        print("\n" + "="*50 + "\n")
    
    print("All demonstrations completed!")