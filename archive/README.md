# Archived Files

This directory contains obsolete implementations from v1 and v2 of maph.

## What's Here

- **include/**: Old header files (v1, v2, and intermediate versions)
- **src/**: Old source files (v2 CLI, old perfect hash impl)
- **tests/**: Old test files for previous versions
- **integrations/**: Old integration code (v2 REST API)

## Current Implementation

The current implementation is **v3**, located at:
- `include/maph/v3/` - All v3 headers
- `tests/v3/` - All v3 tests  
- `examples/v3_*.cpp` - V3 examples

## Why Archived

These files were replaced by the cleaner, more composable v3 architecture which:
- Uses modern C++17 features (concepts, std::expected)
- Has better separation of concerns
- Provides composable components (storage, hashers, tables)
- Supports both perfect and standard hashing elegantly

## Restoration

If you need any of these files, they're preserved here and in git history.
