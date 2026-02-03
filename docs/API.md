**Note:** This document describes the v1/v2 API which has been superseded by v3. See `include/maph/` headers for the current API.

# RD-PH Filter API Documentation

## C++ API

### Core Classes

#### `approximate_map<PH, StorageType, Decoder, OutputType>`

Main template class for approximate mappings.

**Template Parameters:**
- `PH`: Perfect hash function type
- `StorageType`: Storage type (uint8_t, uint16_t, uint32_t, uint64_t)
- `Decoder`: Decoder type defining the mapping behavior
- `OutputType`: Output type of the mapping

**Methods:**
```cpp
// Construction
approximate_map(const PH& ph, const StorageType* M, uint64_t m);

// Lookup operations
OutputType operator()(const T& x) const;
bool contains(const T& x) const;  // For set membership decoders

// Properties
uint64_t size() const;
double false_positive_rate() const;
double false_negative_rate() const;
```

#### `ApproxMapBuilder<PH>`

Builder class for fluent construction.

**Methods:**
```cpp
// Configuration
ApproxMapBuilder& with_load_factor(double factor);
ApproxMapBuilder& with_error_rate(double rate);

// Building filters
template <typename StorageType>
auto build_set_filter(const Range& elements);

template <typename StorageType>
auto build_threshold_filter(const Range& elements, double target_fpr);

template <typename StorageType, typename ValueRange>
auto build_lookup(const Range& keys, const ValueRange& values);
```

### Decoders

#### `SetMembershipDecoder<StorageType, H>`
```cpp
bool operator()(StorageType value, H max_val) const;
```

#### `ThresholdDecoder<StorageType, H>`
```cpp
ThresholdDecoder(StorageType threshold);
bool operator()(StorageType value, H max_val) const;
```

#### `IdentityDecoder<StorageType, H>`
```cpp
StorageType operator()(StorageType value, H max_val) const;
```

### Lazy Iterators

#### `lazy_generator_iterator<Generator>`
```cpp
template <typename Generator>
class lazy_generator_iterator {
    lazy_generator_iterator(Generator gen, size_t start, size_t end);
    value_type operator*() const;
};
```

#### `filter_iterator<Iterator, Predicate>`
```cpp
template <typename Iterator, typename Predicate>
class filter_iterator {
    filter_iterator(Iterator begin, Iterator end, Predicate pred);
};
```

#### `transform_iterator<Iterator, Transform>`
```cpp
template <typename Iterator, typename Transform>
class transform_iterator {
    transform_iterator(Iterator it, Transform transform);
};
```

### Convenience Functions

```cpp
// Create set membership filter
template <typename StorageType = uint32_t>
auto create_set_filter(const Range& elements, double error_rate = 0.0);

// Create threshold filter
template <typename StorageType = uint32_t>
auto create_threshold_filter(const Range& elements, double target_fpr);

// Create compact lookup
template <typename StorageType = uint32_t>
auto create_compact_lookup(const Keys& keys, const Values& values);
```

## Python API

### Module: `approximate_filters`

#### Filter Classes

##### `ApproxFilter8` / `ApproxFilter16` / `ApproxFilter32` / `ApproxFilter64`

Set membership filters with different storage sizes.

```python
class ApproxFilter8:
    def __init__(self, elements, builder):
        """Create 8-bit filter from elements"""
    
    def contains(self, x) -> bool:
        """Check if x is in the filter"""
    
    def __contains__(self, x) -> bool:
        """Python 'in' operator support"""
    
    @property
    def fpr(self) -> float:
        """False positive rate"""
    
    def storage_bytes(self) -> int:
        """Storage size in bytes"""
    
    def false_negative_rate(self) -> float:
        """False negative rate"""
```

##### `ThresholdFilter8` / `ThresholdFilter16` / `ThresholdFilter32` / `ThresholdFilter64`

Threshold filters with configurable FPR.

```python
class ThresholdFilter32:
    def __init__(self, elements, builder, target_fpr: float):
        """Create threshold filter with target FPR"""
    
    def contains(self, x) -> bool:
        """Check membership"""
```

##### `CompactLookup8` / `CompactLookup16` / `CompactLookup32` / `CompactLookup64`

Compact lookup tables.

```python
class CompactLookup32:
    def __init__(self, keys, values, builder):
        """Create compact lookup table"""
    
    def __getitem__(self, key):
        """Get value for key"""
    
    def get(self, key, default=None):
        """Get value with default"""
    
    def storage_bytes(self) -> int:
        """Storage size in bytes"""
```

#### Builder Classes

##### `PerfectHashBuilder`

```python
class PerfectHashBuilder:
    def __init__(self, error_rate: float = 0.0):
        """Create perfect hash builder with error rate"""
```

##### `ApproxMapBuilder`

```python
class ApproxMapBuilder:
    def __init__(self, ph_builder):
        """Create approximate map builder"""
    
    def with_load_factor(self, factor: float):
        """Set load factor (returns self)"""
    
    def build_filter_8bit(self, elements):
        """Build 8-bit filter"""
    
    def build_filter_16bit(self, elements):
        """Build 16-bit filter"""
    
    def build_filter_32bit(self, elements):
        """Build 32-bit filter"""
    
    def build_filter_64bit(self, elements):
        """Build 64-bit filter"""
```

#### Convenience Functions

```python
def create_filter(elements, bits=32, error_rate=0.0):
    """Create a filter with specified bit width"""

def create_threshold_filter(elements, target_fpr, bits=32):
    """Create threshold filter with target FPR"""

def create_lookup(keys, values, bits=32):
    """Create compact lookup table"""
```

#### Constants

```python
FPR_8BIT = 0.00390625   # 1/256
FPR_16BIT = 1.52587890625e-05  # 1/65536
FPR_32BIT = 2.3283064365386963e-10  # 1/2^32
FPR_64BIT = 5.421010862427522e-20  # 1/2^64

__version__ = "2.0.0"
```

## Usage Examples

### C++ Examples

#### Basic Set Membership
```cpp
#include <rd_ph_filter/approximate_map.hpp>

std::vector<std::string> allowed = {"alice", "bob", "charlie"};
auto filter = create_set_filter<uint32_t>(allowed);

if (filter.contains("alice")) {
    std::cout << "User allowed\n";
}
```

#### Threshold Filter
```cpp
auto filter = create_threshold_filter<uint16_t>(elements, 0.05);
// 5% false positive rate with 16-bit storage
```

#### Compact Lookup
```cpp
std::unordered_map<std::string, int> data = {
    {"red", 0xFF0000},
    {"green", 0x00FF00},
    {"blue", 0x0000FF}
};

auto lookup = create_compact_lookup<uint32_t>(
    keys_from_map(data),
    values_from_map(data)
);

int rgb = lookup("red");  // 0xFF0000
```

#### Lazy Iteration
```cpp
auto lazy_gen = lazy_generator_iterator(
    [](size_t i) { return compute_element(i); },
    0, 1000000
);

auto filter = create_set_filter<uint8_t>(
    lazy_gen.begin(), lazy_gen.end()
);
```

### Python Examples

#### Basic Usage
```python
import approximate_filters as af

# Create filter
elements = ["apple", "banana", "cherry"]
filter = af.create_filter(elements, bits=32)

# Test membership
if "apple" in filter:
    print("Found apple")

# Check properties
print(f"Storage: {filter.storage_bytes()} bytes")
print(f"FPR: {filter.fpr:.2%}")
```

#### Threshold Filter
```python
# Create filter with 5% FPR
allowed_ips = ["192.168.1.1", "10.0.0.1", "172.16.0.1"]
filter = af.create_threshold_filter(allowed_ips, target_fpr=0.05, bits=16)

if ip_address in filter:
    process_request()
```

#### Compact Lookup
```python
# Create compact color palette
colors = {
    "red": 0xFF0000,
    "green": 0x00FF00,
    "blue": 0x0000FF
}

lookup = af.create_lookup(
    list(colors.keys()),
    list(colors.values()),
    bits=32
)

rgb = lookup["red"]  # 0xFF0000
```

#### Builder Pattern
```python
# Configure and build
builder = af.PerfectHashBuilder(error_rate=0.01)
map_builder = af.ApproxMapBuilder(builder)
map_builder = map_builder.with_load_factor(1.5)

filter = map_builder.build_filter_32bit(elements)
```

## Error Handling

### C++ Exceptions
- `std::invalid_argument`: Invalid parameters
- `std::runtime_error`: Construction failures
- `std::out_of_range`: Lookup failures (for non-set membership)

### Python Exceptions
- `ValueError`: Invalid parameters or mismatched sizes
- `KeyError`: Key not found in lookup (when not using get())
- `RuntimeError`: Construction or lookup failures