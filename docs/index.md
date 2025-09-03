# maph - Map Perfect Hash

Space-efficient approximate mappings using perfect hash functions.

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://isocpp.org/std/the-standard)
[![Python 3.7+](https://img.shields.io/badge/Python-3.7%2B-blue.svg)](https://www.python.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

## What is maph?

**maph** (map perfect hash) is a generalized C++ framework with Python bindings for creating space-efficient approximate data structures. It supports arbitrary function approximation (f: X → Y) with configurable storage sizes and accuracy trade-offs.

## Key Features

- 🚀 **Generalized Mappings**: Beyond set membership - support any function X → Y
- 📦 **Configurable Storage**: Choose 8/16/32/64-bit storage based on your needs
- 🎯 **Tunable Accuracy**: Control false positive rates through storage size or thresholds
- 🔧 **Flexible CLI**: Command-line tool for practical use without coding
- 🐍 **Python Integration**: Full-featured Python bindings with numpy support
- ⚡ **High Performance**: C++17 header-only library with zero-overhead abstractions

## Quick Start

### Installation

```bash
# Clone the repository
git clone https://github.com/queelius/maph.git
cd maph

# Build C++ library and CLI
mkdir build && cd build
cmake .. -DBUILD_CLI=ON
make

# Install Python bindings
pip install -e .
```

### Command-Line Usage

```bash
# Simple key-value mapping
echo -e "alice,100\nbob,200\ncharlie,300" | maph -b 16

# Multi-dimensional function (x,y,z) -> (a,b)
maph -i data.csv --input-cols 0,1,2 --output-cols 3,4 -b 32

# Save and query models
maph -i data.csv --save model.maph -b 16
maph --load model.maph --query "key1,key2"
```

### Python Usage

```python
import approximate_filters as af

# Create filter with different storage sizes
elements = ["apple", "banana", "cherry"]
filter8 = af.create_filter(elements, bits=8)    # Minimal space
filter32 = af.create_filter(elements, bits=32)  # High accuracy

# Test membership
if "apple" in filter32:
    print("Found apple")

# Compact lookup tables
colors = {"red": 0xFF0000, "green": 0x00FF00, "blue": 0x0000FF}
lookup = af.create_lookup(list(colors.keys()), 
                         list(colors.values()), 
                         bits=16)
rgb = lookup["red"]  # 0xFF0000
```

### C++ Usage

```cpp
#include <maph/approximate_map.hpp>

// Create set membership filter
std::vector<std::string> allowed = {"alice", "bob", "charlie"};
auto filter = create_set_filter<uint32_t>(allowed);

if (filter.contains("alice")) {
    std::cout << "User allowed\n";
}

// Different storage sizes for accuracy/space trade-off
auto filter8 = create_set_filter<uint8_t>(elements);   // 1 byte/elem
auto filter32 = create_set_filter<uint32_t>(elements);  // 4 bytes/elem
```

## Storage Trade-offs

| Storage | Bytes/Element | FPR | Use Case |
|---------|--------------|-----|----------|
| uint8_t | 1 | 0.39% | Large datasets, error-tolerant |
| uint16_t | 2 | 0.0015% | Balanced accuracy/space |
| uint32_t | 4 | ~0% | High accuracy required |
| uint64_t | 8 | ~0% | Cryptographic/security |

## Documentation

- [Architecture Overview](ARCHITECTURE.md) - Design and implementation details
- [API Reference](API.md) - Complete API documentation
- [Examples](https://github.com/queelius/maph/tree/master/examples) - Code examples
- [Python Guide](https://github.com/queelius/maph/tree/master/python/examples) - Python-specific examples

## Use Cases

### Web Services
- **URL deduplication** for web crawlers
- **Cache negative lookups** to avoid expensive misses
- **Rate limiting** with probabilistic counting

### Data Processing
- **Approximate joins** for big data pipelines
- **Data deduplication** in streaming systems
- **Compact lookup tables** for memory-constrained environments

### Security
- **Allowlist/blocklist** checking with minimal memory
- **Spam detection** with compact scoring tables
- **GeoIP filtering** with region-based rules

## Contributing

We welcome contributions! Please see our [Contributing Guide](https://github.com/queelius/maph/blob/master/CONTRIBUTING.md) for details.

## License

This project is licensed under the MIT License - see the [LICENSE](https://github.com/queelius/maph/blob/master/LICENSE) file for details.

## Citation

If you use maph in your research, please cite:

```bibtex
@software{maph2024,
  title = {maph: Map Perfect Hash},
  author = {queelius},
  year = {2024},
  url = {https://github.com/queelius/maph}
}
```

## Links

- [GitHub Repository](https://github.com/queelius/maph)
- [Issue Tracker](https://github.com/queelius/maph/issues)
- [PyPI Package](https://pypi.org/project/maph/) (coming soon)