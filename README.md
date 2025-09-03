# Rate-Distorted Perfect Hash Filter

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/std/the-standard)
[![Python 3.7+](https://img.shields.io/badge/Python-3.7%2B-blue.svg)](https://www.python.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

A generalized C++ framework with Python bindings for space-efficient approximate data structures. Originally designed for set membership testing, now supports arbitrary function approximation (f: X → Y) with configurable storage and accuracy trade-offs.

## Features

- **Generalized Approximate Maps**: Support arbitrary mappings beyond set membership
- **Configurable Storage**: 8/16/32/64-bit storage options with predictable accuracy
- **Custom Decoders**: Extensible decoder interface for specialized applications
- **Lazy Iterators**: Efficient processing of large datasets with lazy evaluation
- **Modern C++17**: Template-based design with zero-overhead abstractions
- **Python Integration**: Comprehensive bindings via pybind11
- **Builder Pattern**: Fluent API for intuitive construction
- **Comprehensive Testing**: Catch2 for C++, pytest for Python

## Theory

The library `rd_ph_filter` (rate-distorted perfect hash filter) models the concept of a Bernoulli set - a probabilistic data structure for set membership testing.

### Rate Distortion Types

1. **False Positive Rate (FPR)**: Occurs when the hash function maps a non-member element to the same value as a member element. This is controlled by the hash function's output space.

2. **False Negative Rate (FNR)**: Occurs when the perfect hash function fails to perfectly hash an element, causing collisions within the objective set. This rate depends on the quality of the perfect hash function.

## Installation

### C++ Library

#### Requirements
- C++17 compatible compiler
- CMake 3.14+
- Optional: Doxygen for documentation

#### Building from Source

```bash
git clone https://github.com/username/rd_ph_filter.git
cd rd_ph_filter
mkdir build && cd build
cmake ..
make
make install
```

#### CMake Integration

Add to your `CMakeLists.txt`:

```cmake
find_package(rd_ph_filter REQUIRED)
target_link_libraries(your_target PRIVATE rd_ph_filter::rd_ph_filter)
```

### Python Package

#### From Source

```bash
pip install .
```

#### Development Installation

```bash
pip install -e .[dev,test]
```

## Usage

### C++ Usage

```cpp
#include <rd_ph_filter/approximate_map.hpp>
#include <vector>

// Basic set membership filter
std::vector<std::string> allowed = {"alice", "bob", "charlie"};
auto filter = create_set_filter<uint32_t>(allowed);

if (filter.contains("alice")) {
    std::cout << "User allowed\n";
}

// Different storage sizes for accuracy/space trade-off
auto filter8 = create_set_filter<uint8_t>(elements);   // 1 byte/element, 0.39% FPR
auto filter32 = create_set_filter<uint32_t>(elements);  // 4 bytes/element, ~0% FPR

// Threshold filter with configurable FPR
auto threshold_filter = create_threshold_filter<uint16_t>(elements, 0.05);

// Compact lookup table
std::unordered_map<std::string, int> colors = {
    {"red", 0xFF0000}, {"green", 0x00FF00}, {"blue", 0x0000FF}
};
auto lookup = create_compact_lookup<uint32_t>(
    keys_from_map(colors), values_from_map(colors)
);
int rgb = lookup("red");  // 0xFF0000

// Using builder pattern
PerfectHashBuilder ph_builder(0.01);
auto filter = ApproxMapBuilder<PerfectHash>(ph_builder)
    .with_load_factor(1.5)
    .build_set_filter<uint32_t>(elements);

// Lazy iterators for large datasets
auto lazy_gen = lazy_generator_iterator(
    [](size_t i) { return expensive_computation(i); },
    0, 1000000
);
auto filter_lazy = create_set_filter<uint8_t>(lazy_gen.begin(), lazy_gen.end());
```

### Python Usage

```python
import approximate_filters as af

# Basic filter with different storage sizes
elements = ["apple", "banana", "cherry"]
filter8 = af.create_filter(elements, bits=8)    # Minimal space, 0.39% FPR
filter32 = af.create_filter(elements, bits=32)  # More space, ~0% FPR

# Test membership
if "apple" in filter32:
    print("Found apple")

# Threshold filter with target FPR
allowed_ips = ["192.168.1.1", "10.0.0.1"]
filter = af.create_threshold_filter(allowed_ips, target_fpr=0.05, bits=16)

# Compact lookup table
colors = {"red": 0xFF0000, "green": 0x00FF00, "blue": 0x0000FF}
lookup = af.create_lookup(list(colors.keys()), list(colors.values()), bits=32)
rgb = lookup["red"]  # 0xFF0000

# Using builder pattern
builder = af.PerfectHashBuilder(error_rate=0.01)
map_builder = af.ApproxMapBuilder(builder).with_load_factor(1.5)
filter = map_builder.build_filter_32bit(elements)

# Check properties
print(f"Storage: {filter.storage_bytes()} bytes")
print(f"FPR: {filter.fpr:.2%}")
```

## Testing

### C++ Tests

```bash
cd build
ctest --verbose
# Or run directly
./tests/rd_ph_filter_tests
```

### Python Tests

```bash
pytest python/tests -v
# With coverage
pytest python/tests --cov=rd_ph_filter --cov-report=html
```

## Documentation

### Building Documentation

```bash
cd build
cmake .. -DBUILD_DOCS=ON
make docs
```

Documentation will be generated in `build/docs/html/`.

## Performance

The rd_ph_filter provides:
- O(1) membership testing
- Space efficiency dependent on the perfect hash function
- Configurable trade-offs between space, false positive rate, and false negative rate

## API Reference

### Core Classes

- `approximate_map<PH, StorageType, Decoder>`: Generalized approximate mapping
- `ApproxMapBuilder<PH>`: Builder for constructing filters and lookups
- `SetMembershipDecoder`: Traditional set membership testing
- `ThresholdDecoder`: Configurable FPR through thresholds
- `IdentityDecoder`: Raw hash value access

### Storage Options

| Type | Storage/Element | FPR | Use Case |
|------|----------------|-----|----------|
| uint8_t | 1 byte | 0.39% | High-volume, error-tolerant |
| uint16_t | 2 bytes | 0.0015% | Balanced accuracy/space |
| uint32_t | 4 bytes | 2.3×10⁻¹⁰ | High accuracy required |
| uint64_t | 8 bytes | 5.4×10⁻²⁰ | Cryptographic applications |

### Key Functions

- `create_set_filter<StorageType>()`: Create set membership filter
- `create_threshold_filter<StorageType>()`: Create filter with target FPR
- `create_compact_lookup<StorageType>()`: Create compact key-value mapping

### Python Module

- `approximate_filters`: Main module with filter classes
- `ApproxFilter8/16/32/64`: Set membership filters
- `ThresholdFilter8/16/32/64`: Threshold-based filters
- `CompactLookup8/16/32/64`: Compact lookup tables

## Contributing

Contributions are welcome! Please ensure:
1. All tests pass
2. Code follows the existing style
3. Documentation is updated
4. Commit messages are descriptive

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Future Work

- Packed matrix storage for arbitrary bit-width hashes
- GPU acceleration for batch operations
- Serialization support
- Additional perfect hash function implementations
