# TDD Review Report: maph Perfect Hash Library

## Executive Summary

This report documents the findings of a comprehensive Test-Driven Development (TDD) focused review of the maph repository, a C++23 header-only library implementing multiple perfect hash algorithms.

**Review Date:** 2024-11-24
**Reviewer:** Claude (Opus 4.5)
**Files Reviewed:**
- `include/maph/hashers_perfect.hpp` - Perfect hash implementations
- `include/maph/hashers.hpp` - Basic hash implementations
- `include/maph/core.hpp` - Core types and concepts
- `include/maph/storage.hpp` - Storage backends
- `include/maph/table.hpp` - Hash table implementation
- `tests/v3/test_perfect_hash.cpp` - Existing tests
- `tests/v3/*.cpp` - Other test files

---

## 1. Critical Defects (Must Fix)

### 1.1 Not True Perfect Hash Functions

**Severity:** Critical
**Affected Components:** RecSplit, CHD, FCH

**Description:**
The implementations of RecSplit, CHD, and FCH are not true perfect hash functions. They store the full keys in `std::unordered_map` data structures, which means:

1. Space complexity is O(n * avg_key_length), not the theoretical O(n * bits_per_key)
2. The `bits_per_key` statistic is misleading (reports only auxiliary structure size)
3. Query time includes string hashing and map lookup, not the theoretical O(1)

**Evidence:**
```cpp
// From hashers_perfect.hpp - RecSplit stores keys:
std::unordered_map<std::string, slot_index> key_to_slot_;

// From hashers_perfect.hpp - CHD stores keys:
std::unordered_map<std::string, slot_index> key_to_slot_;

// From hashers_perfect.hpp - FCH stores keys:
std::unordered_map<std::string, slot_index> key_to_slot_;
```

**Impact:**
- Documentation claims "~2 bits per key" but actual space is much higher
- Users expecting minimal memory usage will be surprised
- Benchmarks comparing to other implementations will be misleading

**Recommendation:**
Either:
1. Implement true perfect hash construction (recommended)
2. Update documentation to accurately describe the current implementation as a "lookup table with perfect hash interface"

### 1.2 Test File API Mismatch

**Severity:** High
**Affected Components:** `test_storage.cpp`, `test_table.cpp`

**Description:**
Test files use outdated API that doesn't match current implementation:

```cpp
// Tests use:
storage.slot_count()

// Implementation provides:
storage.get_slot_count()
```

Also:
- Tests access private `slot_type` typedef
- Tests have missing `<random>` include in `test_table.cpp`

**Impact:**
- `test_v3_comprehensive` target fails to build
- CI/CD pipelines likely broken

**Recommendation:**
1. Update test files to use `get_slot_count()`
2. Make `slot_type` public or add a `slot_size()` accessor
3. Add missing includes

---

## 2. Non-Critical Issues (Should Fix)

### 2.1 BBHash Build Fragility with Low Levels

**Severity:** Medium
**Affected Components:** BBHash (`bbhash_hasher`)

**Description:**
BBHash with only 3 levels (`bbhash3`) frequently fails to build for key sets >100 keys. The algorithm needs more collision resolution levels or higher gamma values for reliability.

**Evidence:**
```
Test: "BBHash: Rank at multiple word boundaries" (200 keys)
Result: build() returns error::optimization_failed with bbhash3
Fix: Using bbhash5 with gamma=2.5 succeeds
```

**Recommendation:**
- Default to 5 levels instead of 3 for general use
- Add clear documentation about level/gamma requirements for different key set sizes
- Consider automatic retry with different parameters

### 2.2 Incomplete Deserialization

**Severity:** Medium
**Affected Components:** `minimal_perfect_hasher`

**Description:**
The `deserialize()` method returns `error::invalid_format` without actually parsing the data:

```cpp
inline result<minimal_perfect_hasher> minimal_perfect_hasher::deserialize(std::span<const std::byte> data) {
    // Simplified deserialization
    if (data.size() < sizeof(uint64_t)) {
        return std::unexpected(error::invalid_format);
    }
    // This is a stub - full implementation would parse the serialized format
    return std::unexpected(error::invalid_format);  // Always fails!
}
```

**Recommendation:**
- Implement full deserialization or
- Mark the method as `= delete` or throw `not_implemented`

### 2.3 Statistics Memory Calculation Inconsistency

**Severity:** Low
**Affected Components:** All perfect hashers

**Description:**
The `statistics()` methods report memory usage inconsistently:
- Some include `sizeof(*this)` in the calculation
- Some count only auxiliary structures
- None account for the stored keys (which is the dominant cost)

**Recommendation:**
- Standardize memory reporting across all hashers
- Add separate fields for "structure overhead" vs "key storage"

### 2.4 Missing Copy Constructors

**Severity:** Low
**Affected Components:** Perfect hashers

**Description:**
All perfect hashers have move semantics but copy is implicitly deleted. This is probably intentional but not documented.

**Recommendation:**
- Add `= delete` explicitly to document the intent
- Or implement copy semantics if needed

---

## 3. Improvements (Nice to Have)

### 3.1 Add Thread Parameter Documentation

The `with_threads()` builder method for BBHash doesn't actually use multiple threads in the current implementation.

### 3.2 Better Error Messages

The `error::optimization_failed` error doesn't indicate why construction failed. Adding error context would help users debug build failures.

### 3.3 Add Configuration Hints

Automatically suggest better parameters when build fails (e.g., "try increasing gamma or number of levels").

### 3.4 Consistent Naming

- `max_slots()` vs `slot_count()` vs `get_slot_count()` - standardize
- `key_count()` vs `statistics().key_count` - redundant

---

## 4. Test Coverage Analysis

### 4.1 Well-Covered Areas

- Basic functionality (single key, small sets, medium sets)
- Empty key handling
- Duplicate key deduplication
- Builder fluent interface
- Factory functions
- Performance benchmarks

### 4.2 Coverage Gaps Identified and Fixed

The following gaps were identified and new tests added in `test_perfect_hash_extended.cpp`:

| Gap | New Test |
|-----|----------|
| Word boundary rank calculation | "BBHash: Rank at word boundary" |
| Binary keys with nulls | "Binary keys with null bytes" |
| Unknown key rejection | "All hashers: Must reject unknown keys" |
| Fingerprint false positive rate | "Fingerprint: Low false positive rate" |
| Determinism with same seed | "Determinism: Same seed produces same hash function" |
| Default constructor safety | "Default constructed hashers are safe" |
| Move semantics | "Move semantics preserve functionality" |
| hash()/slot_for() consistency | "hash() and slot_for() return consistent values" |
| is_perfect_for() consistency | "is_perfect_for() matches slot_for() behavior" |
| Two keys edge case | "Two keys - minimal non-trivial case" |
| All duplicates | "All duplicate keys - should deduplicate to one" |
| Power of two sizes | "Power of two key counts" |
| Prime number sizes | "Prime number key counts" |
| 100 build stress test | "Stress: Build 100 different hash functions" |
| Very long keys | "Stress: Maximum key length stress" |

### 4.3 Test Files Created

**New File:** `/home/spinoza/github/released/maph/tests/v3/test_perfect_hash_extended.cpp`
- 27 new test cases
- ~700 lines of TDD-focused tests
- Tests edge cases, properties, and invariants

---

## 5. Recommendations Summary

### Immediate Actions (P0)

1. Fix test file API mismatches so `test_v3_comprehensive` builds
2. Update documentation to reflect actual space complexity

### Short-term Actions (P1)

1. Implement true perfect hash construction (without key storage)
2. Default BBHash to 5 levels
3. Implement deserialization or mark as not implemented

### Long-term Actions (P2)

1. Add comprehensive documentation of algorithm trade-offs
2. Add automatic parameter tuning on build failure
3. Standardize memory reporting

---

## 6. Test Execution Summary

### Original Tests
```
All tests passed (1263 assertions in 69 test cases)
```

### Extended Tests
```
All tests passed (1091 assertions in 27 test cases)
```

### Total Test Coverage After Review
- 96 test cases for perfect hash functionality
- 2354 assertions
- All passing

---

## 7. Files Modified/Created

### Created
- `/home/spinoza/github/released/maph/tests/v3/test_perfect_hash_extended.cpp` - Extended TDD tests
- `/home/spinoza/github/released/maph/docs/TDD_REVIEW_REPORT.md` - This report

### Modified
- `/home/spinoza/github/released/maph/tests/v3/CMakeLists.txt` - Added new test target

---

## 8. Code Snippets for Critical Fixes

### Fix for Test API Mismatch

```cpp
// In test_storage.cpp, change:
REQUIRE(storage.slot_count().value == count.value);
// To:
REQUIRE(storage.get_slot_count().value == count.value);
```

### Fix for Missing Include

```cpp
// In test_table.cpp, add:
#include <random>
```

### Fix for Private Type Access

Either make the type public:
```cpp
template<size_t SlotSize = 512>
class heap_storage {
public:
    using slot_type = slot<SlotSize>;  // Now public
    // ...
};
```

Or add an accessor:
```cpp
static constexpr size_t slot_data_size = slot_type::data_size;
```

---

## Conclusion

The maph library has a solid foundation with good test coverage for basic functionality. The main concern is that the "perfect hash" implementations are not true minimal perfect hash functions - they store keys directly, resulting in O(n) space rather than the theoretical O(n) bits. This should be either fixed or clearly documented.

The new extended tests add 27 additional test cases covering edge cases, property-based invariants, and stress scenarios that were previously untested. All 96 combined test cases pass successfully.
