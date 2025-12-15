# GEMINI.md - `maph` Project Context

This document provides essential context for the `maph` project, a high-performance, memory-mapped approximate perfect hash database.

## Project Overview

`maph` is a C++ project that provides a memory-mapped, approximate perfect hash map. It's designed for high-performance, O(1) lookups, and concurrent access. The core of the project is a header-only library, but it also includes a command-line interface (CLI), a REST API server, and benchmarks.

**Key Technologies:**

*   **Language:** C++23
*   **Build System:** CMake
*   **Concurrency:** OpenMP
*   **Key Features:**
    *   Memory-mapped storage for fast access
    *   Perfect hash functions for O(1) lookups
    *   Approximate data structures for space efficiency
    *   Lock-free operations for concurrent access
    *   REST API for a key-value store

## Building and Running

The project uses CMake for building. The following commands can be used to build the project:

```bash
# Clone the repository
git clone https://github.com/yourusername/maph.git
cd maph

# Build everything
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release \
         -DBUILD_TESTS=ON \
         -DBUILD_REST_API=ON \
         -DBUILD_EXAMPLES=ON
make -j$(nproc)

# Run tests
ctest --verbose

# Install
sudo make install
```

### CMake Options

| Option           | Default | Description                   |
| ---------------- | ------- | ----------------------------- |
| `BUILD_TESTS`    | `OFF`   | Build test suite              |
| `BUILD_EXAMPLES` | `OFF`   | Build example programs        |
| `BUILD_REST_API` | `OFF`   | Build REST API server         |
| `BUILD_DOCS`     | `OFF`   | Generate documentation        |

## Development Conventions

*   The core library is header-only and located in `include/maph`.
*   The code uses C++23 features like `std::expected`.
*   Tests are written using the Catch2 framework and are located in the `tests` directory.
*   Benchmarks are located in the `benchmarks` directory.
*   The project follows the Google C++ Style Guide.

## Command-Line Interface (CLI)

The `maph_cli` executable provides a way to interact with `maph` databases from the command line.

**Usage:** `maph_cli <command> [arguments] [options]`

**Commands:**

*   `create <file> <slots>`: Create a new maph file.
*   `set <file> <key> <value>`: Set a key-value pair.
*   `get <file> <key>`: Get the value for a key.
*   `remove <file> <key>`: Remove a key.
*   `stats <file>`: Show database statistics.
*   `optimize <file>`: Optimize the database with perfect hashing.
*   `bench <file>`: Run a benchmark.
*   `bench_parallel <file> [threads]`: Run a parallel benchmark.
*   `load_bulk <file> <jsonl>`: Load a JSONL file.
*   `mget <file> <key1> ...`: Get multiple keys.
*   `mset <file> k1 v1 k2 v2...`: Set multiple key-value pairs.

**Options:**

*   `--threads <n>`: Thread count for parallel operations.
*   `--durability <ms>`: Enable async durability.

**Examples:**

```bash
# Create a database with 1 million slots
maph_cli create data.maph 1000000

# Set a key-value pair
maph_cli set data.maph '{"id":1}' '{"name":"alice"}'

# Get the value for a key
maph_cli get data.maph '{"id":1}'
```
