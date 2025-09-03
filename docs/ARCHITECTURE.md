# Rate-Distorted Perfect Hash Filter - Architecture Documentation

## Overview

The Rate-Distorted Perfect Hash (RD-PH) Filter is a generalized framework for creating space-efficient approximate data structures. Originally designed for set membership testing, it has been generalized to support arbitrary function approximation (f: X → Y) with configurable storage and accuracy trade-offs.

## Core Concepts

### 1. Generalized Approximate Maps

The framework has evolved from simple set membership to support arbitrary mappings:

```cpp
template <typename PH, typename StorageType, typename Decoder, typename OutputType>
class approximate_map {
    // Maps elements of type X to values of type Y
    // Uses StorageType bits per element
    // Decoder defines the mapping behavior
};
```

### 2. Storage Types

Different storage sizes provide different accuracy guarantees:

- **8-bit** (uint8_t): ~0.39% FPR, minimal space
- **16-bit** (uint16_t): ~0.0015% FPR, low space
- **32-bit** (uint32_t): ~2.3×10⁻¹⁰ FPR, moderate space
- **64-bit** (uint64_t): ~5.4×10⁻²⁰ FPR, high accuracy

### 3. Custom Decoders

Decoders define how hash values map to output values:

#### SetMembershipDecoder
Traditional set membership testing (returns bool).

```cpp
template <typename StorageType, typename H>
struct SetMembershipDecoder {
    bool operator()(StorageType value, H max_val) const {
        return value < max_val;
    }
};
```

#### ThresholdDecoder
Configurable false positive rate through threshold adjustment.

```cpp
template <typename StorageType, typename H>
struct ThresholdDecoder {
    StorageType threshold;
    bool operator()(StorageType value, H) const {
        return value < threshold;
    }
};
```

#### IdentityDecoder
Returns the raw hash value for custom processing.

```cpp
template <typename StorageType, typename H>
struct IdentityDecoder {
    StorageType operator()(StorageType value, H) const {
        return value;
    }
};
```

### 4. Lazy Iterators

Support for lazy computation through custom iterators:

- **lazy_generator_iterator**: Generate values on-demand
- **filter_iterator**: Filter elements based on predicates
- **transform_iterator**: Transform elements during iteration
- **sampling_iterator**: Random sampling from datasets
- **chain_iterator**: Chain multiple iterators

## Architecture Layers

### Layer 1: Perfect Hash Function
- Provides minimal perfect hash functions
- Configurable error rate (false negative rate)
- Based on RecSplit algorithm

### Layer 2: Storage Management
- Template-based storage types (8/16/32/64-bit)
- Efficient memory layout
- Load factor configuration for sparse storage

### Layer 3: Decoding Layer
- Custom decoders for different use cases
- Extensible decoder interface
- Support for arbitrary output types

### Layer 4: High-Level API
- Builder pattern for fluent construction
- Python bindings via pybind11
- Convenience functions for common use cases

## Design Patterns

### Builder Pattern

```cpp
auto filter = ApproxMapBuilder<PerfectHash>()
    .with_load_factor(1.5)
    .with_error_rate(0.01)
    .build_set_filter<uint32_t>(elements);
```

### Template Specialization

Storage types are selected at compile-time for optimal performance:

```cpp
using Filter8 = approximate_map<PH, uint8_t, SetMembershipDecoder<uint8_t, PH::H>>;
using Filter16 = approximate_map<PH, uint16_t, SetMembershipDecoder<uint16_t, PH::H>>;
```

### Lazy Evaluation

Iterators enable efficient processing of large datasets:

```cpp
auto lazy_data = lazy_generator_iterator([](size_t i) { 
    return expensive_computation(i); 
});
```

## Performance Characteristics

### Space Complexity
- O(n) storage where n is the number of elements
- Bits per element determined by StorageType
- No overhead for storing keys

### Time Complexity
- Construction: O(n) expected time
- Lookup: O(1) expected time
- Iteration: O(n) with lazy evaluation support

### Trade-offs

| Storage | Space/Element | FPR | Use Case |
|---------|--------------|-----|----------|
| 8-bit | 1 byte | 0.39% | High-volume, error-tolerant |
| 16-bit | 2 bytes | 0.0015% | Balanced accuracy/space |
| 32-bit | 4 bytes | 2.3×10⁻¹⁰ | High accuracy required |
| 64-bit | 8 bytes | 5.4×10⁻²⁰ | Cryptographic applications |

## Extension Points

### Custom Decoders

Implement new decoders by following this interface:

```cpp
template <typename StorageType, typename H>
struct CustomDecoder {
    using output_type = YourOutputType;
    
    output_type operator()(StorageType value, H max_val) const {
        // Your decoding logic
    }
};
```

### Custom Iterators

Create domain-specific iterators:

```cpp
template <typename T>
class custom_iterator {
    // Iterator requirements
    using value_type = T;
    using reference = T;
    using pointer = T*;
    // Implementation...
};
```

## Migration Guide

### From Original API

Original set membership API:
```cpp
rd_ph_filter<uint32_t> filter(elements);
if (filter.contains(x)) { /* ... */ }
```

New generalized API:
```cpp
auto filter = create_set_filter<uint32_t>(elements);
if (filter.contains(x)) { /* ... */ }
```

### Python Migration

Original Python API:
```python
filter = RdPhFilter(elements)
if x in filter: # ...
```

New Python API:
```python
import approximate_filters as af
filter = af.create_filter(elements, bits=32)
if x in filter: # ...
```

## Best Practices

1. **Choose appropriate storage size** based on your accuracy requirements
2. **Use lazy iterators** for large datasets to avoid memory overhead
3. **Consider custom decoders** for specialized applications
4. **Profile your use case** to find optimal load factors
5. **Batch operations** when possible for better cache locality

## Future Directions

- GPU acceleration for massive datasets
- Distributed perfect hash computation
- Adaptive storage selection based on data characteristics
- Integration with columnar storage formats
- Support for dynamic updates (insertions/deletions)