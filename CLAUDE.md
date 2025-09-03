# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository Overview

The `rd_ph_filter` library is a generalized C++ framework for space-efficient approximate data structures with Python bindings. Originally designed for set membership testing, it now supports arbitrary function approximation (f: X → Y) with configurable storage sizes (8/16/32/64-bit) and custom decoders. The framework provides controllable trade-offs between space, accuracy, and computation.

## Common Development Commands

### Building the C++ Library

```bash
# Configure and build
mkdir build && cd build
cmake .. -DBUILD_TESTS=ON -DBUILD_PYTHON_BINDINGS=ON -DBUILD_EXAMPLES=ON
make -j$(nproc)

# Run all C++ tests
ctest --verbose --output-on-failure
# Or directly:
./tests/rd_ph_filter_tests
./tests/test_approximate_map
./tests/test_lazy_iterators

# Run specific test suites
./tests/test_approximate_map "[SetMembership]"
./tests/test_approximate_map "[ThresholdFilter]"
./tests/test_approximate_map "[CompactLookup]"

# Run examples
./examples/demo_approximate_map
./examples/demo_lazy_iterators
./examples/demo_custom_decoders

# Build with documentation
cmake .. -DBUILD_DOCS=ON
make docs
```

### Python Development

```bash
# Install for development (from repository root)
pip install -e .

# Run Python tests
pytest python/tests -v
pytest python/tests/test_approximate_filters.py -v

# Run with coverage
pytest python/tests --cov=approximate_filters --cov-report=html

# Run specific test classes
pytest python/tests/test_approximate_filters.py::TestBasicFilters -v
pytest python/tests/test_approximate_filters.py::TestThresholdFilters -v
pytest python/tests/test_approximate_filters.py::TestCompactLookup -v

# Run Python examples
python python/examples/demo_approximate_filters.py

# Format Python code
black python/

# Type checking
mypy python/
```

### Code Quality

```bash
# Format C++ code (if clang-format available)
find include tests -name "*.hpp" -o -name "*.cpp" | xargs clang-format -i

# Static analysis (if clang-tidy available)
clang-tidy include/**/*.hpp -- -std=c++17 -I include

# Check Python code
flake8 python/
black --check python/
```

## Architecture

### Core Components

1. **approximate_map.hpp**: Generalized approximate mapping framework
   - Template-based design supporting arbitrary mappings (X → Y)
   - Configurable storage types (8/16/32/64-bit)
   - Custom decoder support for specialized applications
   - O(1) lookup operations

2. **rd_ph_filter.hpp**: Original set membership filter
   - Specialized for boolean membership testing
   - Template-based design with perfect hash function parameter

3. **builder.hpp**: Fluent API and builder patterns
   - `ApproxMapBuilder`: Generalized builder for filters and lookups
   - `rd_ph_filter_builder`: Original filter construction
   - Support for load factor and error rate configuration

4. **lazy_iterators.hpp**: Lazy evaluation support
   - `lazy_generator_iterator`: Generate values on-demand
   - `filter_iterator`: Filter elements with predicates
   - `transform_iterator`: Transform during iteration
   - `sampling_iterator`: Random sampling
   - `chain_iterator`: Chain multiple iterators

5. **Python Bindings** (python/src/)
   - `ph_wrapper.hpp`: Python-compatible perfect hash wrapper
   - `bindings.cpp`: Original rd_ph_filter bindings
   - `bindings_v2.cpp`: New approximate_filters module with full API

### Key Design Patterns

- **Template-based generic programming**: Works with any perfect hash function and storage type
- **Policy-based design**: Decoders define mapping behavior
- **Builder pattern**: Flexible configuration before construction
- **Fluent interface**: Chainable method calls for intuitive API
- **RAII**: Automatic resource management
- **Immutability**: Filter state cannot be modified after construction
- **Lazy evaluation**: Iterator-based computation on demand

### Perfect Hash Function Requirements

Any perfect hash function `PH` must provide:
- `operator()`: Maps elements to indices
- `max_hash()`: Returns maximum hash value
- `error_rate()`: Returns collision probability
- `hash_fn()`: Returns underlying hash function
- Nested type `H` with `hash_type` typedef

## Testing Strategy

### C++ Tests (tests/)
- Unit tests using Catch2 framework
- Mock perfect hash implementation for controlled testing
- Coverage of core functionality, edge cases, and stress tests

### Python Tests (python/tests/)
- Integration tests using pytest
- Tests for all binding functionality
- Performance and stress tests with large datasets

## Common Tasks

### Adding a New Decoder

1. Create decoder struct in `include/rd_ph_filter/`
```cpp
template <typename StorageType, typename H>
struct MyDecoder {
    using output_type = MyOutputType;
    output_type operator()(StorageType value, H max_val) const;
};
```
2. Add tests in `tests/test_approximate_map.cpp`
3. Update Python bindings if needed
4. Add examples showing usage

### Adding a New Storage Type

1. Template instantiation in `approximate_map.hpp`
2. Python bindings in `bindings_v2.cpp`
3. Add filter classes (e.g., `ApproxFilterN`)
4. Update tests and documentation

### Creating Custom Iterators

1. Implement iterator requirements in `lazy_iterators.hpp`
2. Provide value_type, reference, pointer typedefs
3. Implement increment, dereference, comparison
4. Add tests and examples

### Extending the API

1. Add methods to appropriate class in `include/`
2. Add Doxygen documentation
3. Add unit tests
4. Update Python bindings in `python/src/bindings_v2.cpp`
5. Add Python tests in `test_approximate_filters.py`
6. Update README and API documentation

### Debugging Build Issues

```bash
# Verbose CMake output
cmake .. -DCMAKE_VERBOSE_MAKEFILE=ON

# Debug build
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Check Python modules
python -c "import rd_ph_filter; print(rd_ph_filter.__version__)"
python -c "import approximate_filters as af; print(af.__version__)"

# Test specific functionality
python -c "
import approximate_filters as af
f = af.create_filter([1,2,3], bits=32)
print(f'Storage: {f.storage_bytes()} bytes, FPR: {f.fpr}')
"
```

## Performance Considerations

- Filter construction is O(n) where n is the number of elements
- Lookup operations are O(1) expected time
- Space complexity: StorageType size × n elements (no key storage)
- Python bindings add minimal overhead for individual operations
- Batch operations amortize Python/C++ transition costs

### Storage Trade-offs

| Storage | Bytes/Element | FPR | Speed | Use Case |
|---------|--------------|-----|-------|----------|
| uint8_t | 1 | 0.39% | Fastest | Large datasets, error-tolerant |
| uint16_t | 2 | 0.0015% | Fast | Balanced accuracy/space |
| uint32_t | 4 | ~0% | Fast | High accuracy required |
| uint64_t | 8 | ~0% | Slower | Cryptographic/security |

## Important Notes

1. **Header-only library**: Most components are header-only templates. Changes require recompilation of dependent code.

2. **Template instantiation**: Ensure perfect hash implementations and decoders are fully defined before use.

3. **Python modules**: Two modules available:
   - `rd_ph_filter`: Original API for backward compatibility
   - `approximate_filters`: New generalized API with full features

4. **Python GIL**: Bindings release GIL for long operations when safe.

5. **Thread safety**: Filters are immutable and thread-safe for reading. Construction is not thread-safe.

6. **Error rates**: 
   - False positive rate (FPR) determined by storage size
   - False negative rate (FNR) depends on perfect hash quality
   - Threshold filters allow FPR tuning via threshold parameter

7. **Memory alignment**: Filters use aligned storage for optimal performance.

8. **Lazy evaluation**: Use lazy iterators for large datasets to avoid memory overhead during construction.