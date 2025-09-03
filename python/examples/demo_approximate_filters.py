#!/usr/bin/env python3
"""
Demonstration of the approximate_filters Python module
"""

try:
    import approximate_filters as af
except ImportError:
    print("Please install the module first: pip install -e .")
    exit(1)

import random
import time


def demo_storage_sizes():
    """Demonstrate different storage sizes and their trade-offs"""
    print("\n=== Storage Size Comparison ===\n")
    
    # Create a dataset
    elements = list(range(1000))
    builder = af.PerfectHashBuilder(error_rate=0.0)
    
    # Build filters with different storage sizes
    filter8 = af.ApproxFilter8(elements, builder)
    filter16 = af.ApproxFilter16(elements, builder)
    filter32 = af.ApproxFilter32(elements, builder)
    filter64 = af.ApproxFilter64(elements, builder)
    
    print(f"Dataset: {len(elements)} elements")
    print(f"\n8-bit filter:")
    print(f"  Storage: {filter8.storage_bytes()} bytes")
    print(f"  FPR: {filter8.fpr:.6f} (~{filter8.fpr*100:.4f}%)")
    print(f"  Space savings: {len(elements)*8 / filter8.storage_bytes():.1f}x")
    
    print(f"\n16-bit filter:")
    print(f"  Storage: {filter16.storage_bytes()} bytes")
    print(f"  FPR: {filter16.fpr:.9f} (~{filter16.fpr*100:.6f}%)")
    print(f"  Space savings: {len(elements)*8 / filter16.storage_bytes():.1f}x")
    
    print(f"\n32-bit filter:")
    print(f"  Storage: {filter32.storage_bytes()} bytes")
    print(f"  FPR: {filter32.fpr:.2e}")
    print(f"  Space savings: {len(elements)*8 / filter32.storage_bytes():.1f}x")
    
    print(f"\n64-bit filter:")
    print(f"  Storage: {filter64.storage_bytes()} bytes")
    print(f"  FPR: {filter64.fpr:.2e}")
    print(f"  Space savings: {len(elements)*8 / filter64.storage_bytes():.1f}x")
    
    # Test false positive rates empirically
    print("\n--- Empirical False Positive Test ---")
    non_members = list(range(10000, 20000))
    sample = random.sample(non_members, 1000)
    
    fp8 = sum(1 for x in sample if x in filter8)
    fp16 = sum(1 for x in sample if x in filter16)
    fp32 = sum(1 for x in sample if x in filter32)
    fp64 = sum(1 for x in sample if x in filter64)
    
    print(f"8-bit: {fp8}/1000 false positives ({fp8/10:.1f}%)")
    print(f"16-bit: {fp16}/1000 false positives ({fp16/10:.1f}%)")
    print(f"32-bit: {fp32}/1000 false positives")
    print(f"64-bit: {fp64}/1000 false positives")


def demo_threshold_filter():
    """Demonstrate threshold filters with tunable FPR"""
    print("\n=== Threshold Filter Demo ===\n")
    
    # Create an allowlist
    allowed_users = ["alice", "bob", "charlie", "david", "eve"]
    
    # Create filters with different target FPRs
    for target_fpr in [0.5, 0.2, 0.1, 0.01]:
        filter = af.create_threshold_filter(
            allowed_users, 
            target_fpr=target_fpr,
            bits=32
        )
        
        print(f"Target FPR: {target_fpr*100:.0f}%")
        
        # Test members
        assert all(user in filter for user in allowed_users)
        print(f"  ✓ All allowed users pass")
        
        # Test non-members
        test_users = ["frank", "grace", "henry", "iris", "jack",
                     "kate", "liam", "mia", "noah", "olivia"]
        false_positives = sum(1 for user in test_users if user in filter)
        print(f"  {false_positives}/10 random users allowed (expected ~{target_fpr*10:.1f})")


def demo_compact_lookup():
    """Demonstrate compact lookup tables"""
    print("\n=== Compact Lookup Table Demo ===\n")
    
    # Color palette mapping
    colors = {
        "red": 0xFF0000,
        "green": 0x00FF00,
        "blue": 0x0000FF,
        "yellow": 0xFFFF00,
        "cyan": 0x00FFFF,
        "magenta": 0xFF00FF,
        "white": 0xFFFFFF,
        "black": 0x000000,
    }
    
    keys = list(colors.keys())
    values = list(colors.values())
    
    # Standard dict storage
    dict_size = sum(len(k) for k in keys) + len(values) * 4
    print(f"Standard dict (approx): {dict_size} bytes")
    
    # Compact lookup with 32-bit values
    lookup = af.create_lookup(keys, values, bits=32)
    print(f"Compact lookup: {lookup.storage_bytes()} bytes")
    print(f"Space savings: {dict_size/lookup.storage_bytes():.1f}x")
    
    # Test lookups
    print("\nColor lookups:")
    for color in ["red", "green", "blue"]:
        rgb = lookup[color]
        print(f"  {color}: #{rgb:06X}")
    
    # Test with smaller values - can use 8-bit storage
    priority_map = {
        "critical": 0,
        "high": 1,
        "medium": 2,
        "low": 3,
        "trivial": 4
    }
    
    keys = list(priority_map.keys())
    values = list(priority_map.values())
    
    lookup8 = af.create_lookup(keys, values, bits=8)
    print(f"\nPriority lookup (8-bit): {lookup8.storage_bytes()} bytes")
    print("Priority mappings:")
    for priority in keys:
        print(f"  {priority}: {lookup8[priority]}")


def demo_performance():
    """Demonstrate performance characteristics"""
    print("\n=== Performance Comparison ===\n")
    
    # Create large dataset
    n = 100000
    elements = [f"item_{i:06d}" for i in range(n)]
    builder = af.PerfectHashBuilder(error_rate=0.0)
    
    print(f"Building filters for {n:,} elements...")
    
    # Build and time 8-bit filter
    start = time.time()
    filter8 = af.ApproxFilter8(elements[:10000], builder)  # Subset for speed
    t8 = time.time() - start
    
    # Build and time 32-bit filter
    start = time.time()
    filter32 = af.ApproxFilter32(elements[:10000], builder)
    t32 = time.time() - start
    
    print(f"Build time (10k elements):")
    print(f"  8-bit: {t8*1000:.2f}ms")
    print(f"  32-bit: {t32*1000:.2f}ms")
    
    # Test query performance
    test_samples = random.sample(elements[:10000], 1000)
    
    start = time.time()
    for _ in range(10):
        for x in test_samples:
            _ = x in filter8
    t8_query = (time.time() - start) / 10
    
    start = time.time()
    for _ in range(10):
        for x in test_samples:
            _ = x in filter32
    t32_query = (time.time() - start) / 10
    
    print(f"\nQuery time (1000 lookups):")
    print(f"  8-bit: {t8_query*1000:.2f}ms")
    print(f"  32-bit: {t32_query*1000:.2f}ms")
    
    # Compare with Python set
    pyset = set(elements[:10000])
    start = time.time()
    for _ in range(10):
        for x in test_samples:
            _ = x in pyset
    tset = (time.time() - start) / 10
    
    print(f"  Python set: {tset*1000:.2f}ms")
    
    print(f"\nMemory comparison:")
    print(f"  8-bit filter: {filter8.storage_bytes():,} bytes")
    print(f"  32-bit filter: {filter32.storage_bytes():,} bytes")
    print(f"  Python set (approx): {len(elements[:10000]) * 50:,} bytes")


def demo_builder_pattern():
    """Demonstrate the builder pattern API"""
    print("\n=== Builder Pattern Demo ===\n")
    
    # Create a builder with configuration
    ph_builder = af.PerfectHashBuilder(error_rate=0.01)
    map_builder = af.ApproxMapBuilder(ph_builder)
    
    # Configure with load factor for sparser storage
    map_builder = map_builder.with_load_factor(1.5)
    
    elements = ["apple", "banana", "cherry", "date", "elderberry"]
    
    # Build different variants
    print("Building filters with builder pattern:")
    filter8 = map_builder.build_filter_8bit(elements)
    filter32 = map_builder.build_filter_32bit(elements)
    
    print(f"  8-bit: {filter8.storage_bytes()} bytes")
    print(f"  32-bit: {filter32.storage_bytes()} bytes")
    
    # Test functionality
    for elem in elements:
        assert elem in filter8
        assert elem in filter32
    
    print("  ✓ All elements found in both filters")


def main():
    """Run all demonstrations"""
    print("=" * 60)
    print("Approximate Filters Python Module Demonstration")
    print("=" * 60)
    
    demo_storage_sizes()
    demo_threshold_filter()
    demo_compact_lookup()
    demo_builder_pattern()
    demo_performance()
    
    print("\n" + "=" * 60)
    print("Demonstration complete!")
    print("=" * 60)


if __name__ == "__main__":
    main()