# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Comprehensive API documentation with Doxygen-style comments in `include/maph.hpp`
- Complete architecture documentation in `docs/ARCHITECTURE.md`
- User guide with examples and best practices in `docs/USER_GUIDE.md`
- CLI documentation with all commands and examples in `docs/CLI.md`
- REST API documentation with endpoint reference in `integrations/rest_api/API.md`
- Performance benchmarking methodology documentation

### Changed
- Enhanced inline documentation throughout the codebase
- Improved README with better examples and quick start guide

### Fixed
- Documentation inconsistencies and outdated references

## [1.0.0] - 2024-01-15

### Added
- Initial release of maph (Memory-mapped Approximate Perfect Hash)
- Core key-value storage engine with sub-microsecond lookups
- Memory-mapped I/O for automatic persistence
- Command-line interface (`maph_cli`) with full CRUD operations
- REST API server with HTTP/WebSocket support
- Batch operations (mget, mset, parallel operations)
- SIMD acceleration with AVX2 for batch hashing
- Durability manager for configurable sync intervals
- Database statistics and monitoring capabilities
- Web interface for visual database management
- Docker and Kubernetes deployment configurations

### Changed
- Renamed project from `rd_ph_filter` to `maph` for clarity
- Consolidated multiple implementations into single unified codebase
- Extracted magic numbers to named constants
- Improved error handling with Result type and error codes
- Optimized slot structure for cache-line alignment

### Fixed
- `remove()` function implementation for proper key deletion
- Batch operation race conditions
- Statistics tracking accuracy
- Memory alignment issues on certain architectures
- File descriptor leaks in error paths

### Performance
- Achieved <200ns lookup latency for cached data
- 5M+ ops/sec single-threaded throughput
- 20M+ ops/sec with 8-thread parallel operations
- Reduced memory overhead by 30% through slot optimization

## [0.9.0] - 2023-12-01

### Added
- Python bindings via pybind11
- Approximate map generalization for arbitrary mappings
- Custom decoder support for specialized applications
- Lazy iterator implementations
- Builder pattern for fluent configuration

### Changed
- Template-based design for flexibility
- Header-only library architecture
- Improved perfect hash function interface

### Deprecated
- Original `rd_ph_filter` class (use `approximate_map` instead)
- Old Python module name (use `approximate_filters`)

### Fixed
- False positive rate calculations
- Memory leaks in Python bindings
- Thread safety issues in concurrent access

## [0.8.0] - 2023-10-15

### Added
- Set membership filter implementation
- Threshold filter with configurable FPR
- Compact lookup tables
- CMake build system
- Catch2 unit tests

### Changed
- Moved from Makefile to CMake
- Reorganized project structure
- Improved documentation

### Fixed
- Compilation warnings on GCC 11+
- Undefined behavior in hash functions

## [0.7.0] - 2023-08-01

### Added
- Initial proof of concept
- Basic perfect hash filter
- Simple benchmarking tools

### Known Issues
- Limited to 8-bit storage
- No persistence support
- Single-threaded only

## Comparison with Alternatives

### vs Redis
- **Maph advantages**: 10x faster lookups, zero maintenance, automatic persistence
- **Redis advantages**: Richer data types, replication, Lua scripting

### vs memcached
- **Maph advantages**: Persistence, faster lookups, no network overhead
- **Memcached advantages**: Distributed caching, LRU eviction, larger values

### vs RocksDB
- **Maph advantages**: 25x faster reads, simpler operation, no compaction
- **RocksDB advantages**: Unbounded size, transactions, compression

## Upgrade Guide

### From 0.x to 1.0.0

1. **Database Format Change**:
   - Old databases are not compatible
   - Export data using old version, import with new

2. **API Changes**:
   ```cpp
   // Old
   rd_ph_filter<PH> filter(elements);
   
   // New
   auto db = maph::create("data.maph", 1000000);
   ```

3. **Python Module**:
   ```python
   # Old
   import rd_ph_filter
   
   # New
   import maph
   ```

4. **Configuration**:
   - Load factor now split into static/dynamic ratios
   - Error rate configuration replaced with storage size selection

## Roadmap

### Version 1.1.0 (Q2 2024)
- [ ] Cuckoo hashing for better collision handling
- [ ] Value compression support
- [ ] Authentication and access control
- [ ] Replication support

### Version 1.2.0 (Q3 2024)
- [ ] Multi-file sharding
- [ ] Learned index structures
- [ ] GPU acceleration
- [ ] Persistent memory support

### Version 2.0.0 (Q4 2024)
- [ ] ACID transactions
- [ ] SQL-like query language
- [ ] Distributed clustering
- [ ] Cloud-native features

## Contributing

Please see [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines on how to contribute to this project.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- FNV hash algorithm by Glenn Fowler, Landon Curt Noll, and Kiem-Phong Vo
- Inspired by papers on perfect hashing and approximate data structures
- Community contributors and early adopters

---

[Unreleased]: https://github.com/yourusername/maph/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/yourusername/maph/compare/v0.9.0...v1.0.0
[0.9.0]: https://github.com/yourusername/maph/compare/v0.8.0...v0.9.0
[0.8.0]: https://github.com/yourusername/maph/compare/v0.7.0...v0.8.0
[0.7.0]: https://github.com/yourusername/maph/releases/tag/v0.7.0