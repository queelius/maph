# maph v3 Refactoring: A Study in Elegant Design

## Executive Summary

The maph codebase has been refactored from a monolithic, tightly-coupled implementation into a clean, composable architecture that exemplifies the Unix philosophy. Each component now does one thing well, and components compose orthogonally to create powerful abstractions.

## Key Improvements

### 1. **API Elegance & Simplicity**

#### Before:
- Monolithic `Maph` class with 40+ public methods
- Mixed concerns (storage, hashing, optimization, journaling)
- Inconsistent error handling (nullptr, bool, error codes)
- Primitive obsession throughout

#### After:
- Small, focused components that compose
- Strong types (`slot_index`, `hash_value`, `slot_count`)
- Consistent error handling with `std::expected`
- Clean, intuitive API surface

```cpp
// Before: Confusing, coupled
auto m = Maph::create("db.maph", 1000);
if (!m) { /* handle error */ }
m->set("key", "value");
auto val = m->get("key"); // returns optional

// After: Clear, composable
auto db = maph::create("db.maph", {.slots = slot_count{1000}});
db.and_then([](auto& d) {
    return d.set("key", "value");
});
```

### 2. **Separation of Concerns**

The refactored design separates orthogonal concerns completely:

```
┌─────────────────────────────────────┐
│           Application               │
└─────────────────────────────────────┘
                 │
    ┌────────────┴────────────┐
    │                          │
┌─────────┐            ┌──────────────┐
│  Table  │            │ Optimization │
└─────────┘            └──────────────┘
    │                          │
    ├──────────┬───────────────┘
    │          │
┌─────────┐  ┌─────────┐
│ Hasher  │  │ Storage │
└─────────┘  └─────────┘
```

Each layer is completely independent and composable:
- **Storage**: mmap, heap, cached (decorators work with any backend)
- **Hashing**: FNV-1a, perfect, hybrid (orthogonal to storage)
- **Table**: Combines storage + hashing (simple coordinator)
- **Optimization**: External transformation (not embedded in table)

### 3. **Composability & Orthogonality**

Components compose naturally without coupling:

```cpp
// Any hasher with any storage
auto table1 = make_table(fnv1a_hasher{n}, heap_storage{n});
auto table2 = make_table(perfect_hasher{}, mmap_storage{});

// Decorators compose orthogonally
auto cached_mmap = cached_storage{mmap_storage{}, 1000};
auto journaled = with_journal(table);
auto probing = linear_probe_hasher{base_hasher, 10};
```

### 4. **Modern C++ Idioms**

#### RAII Everywhere:
```cpp
class memory_map {
    void* addr_;
    size_t size_;
public:
    ~memory_map() { if (addr_) ::munmap(addr_, size_); }
    // Move-only, Rule of Five
};
```

#### Strong Types:
```cpp
struct slot_index {
    uint64_t value;
    explicit constexpr slot_index(uint64_t v) : value(v) {}
};
// Prevents: set(value, key) // Compile error!
```

#### Value Semantics:
```cpp
class key {
    std::string_view data_;
public:
    explicit constexpr key(std::string_view sv) : data_(sv) {}
    constexpr auto operator<=>(const key&) const = default;
};
```

### 5. **Error Handling & Safety**

#### Result Types:
```cpp
template<typename T>
using result = std::expected<T, error>;

// Monadic composition
db.set("key", "value")
  .and_then([&](auto) { return db.set("key2", "value2"); })
  .transform([](auto) { /* success */ })
  .or_else([](error e) { /* handle error */ });
```

#### Invalid States Unrepresentable:
- Read-only storage cannot be written to (compile-time check)
- Slots enforce size limits at type level
- Perfect hash guarantees O(1) at type level

### 6. **Template Design**

#### Concepts for Clear Contracts:
```cpp
template<typename H>
concept hasher = requires(H h, std::string_view key) {
    { h.hash(key) } -> std::convertible_to<hash_value>;
    { h.max_slots() } -> std::convertible_to<slot_count>;
};
```

#### Policy-Based Design:
```cpp
template<hasher Hasher, typename Storage>
class hash_table {
    // Policies define behavior
};
```

### 7. **Naming & Consistency**

All names now follow clear patterns:
- Types: `snake_case` (following STL style)
- Strong types: Descriptive (`slot_index`, not `uint64_t`)
- Functions: Verb or verb_noun (`get`, `set`, `make_table`)
- Factories: `make_*` or `create_*`
- Concepts: Descriptive nouns (`hasher`, `storage_backend`)

## Design Principles Demonstrated

### Unix Philosophy
- **Do one thing well**: Each component has a single responsibility
- **Compose simply**: Components combine without coupling
- **Text interface**: Clean string_view based API

### Functional Programming
- **Immutability**: Value types, const-correct
- **Monadic composition**: Error handling with `expected`
- **Pure functions**: Hashers are pure, no side effects

### Generic Programming
- **Zero-cost abstractions**: Templates compile to optimal code
- **Concepts**: Clear contracts without runtime cost
- **Policy-based design**: Behavior injected at compile time

## Performance Characteristics

The refactored design maintains or improves performance:

- **Zero allocations**: On hot paths
- **Cache-friendly**: 64-byte aligned slots
- **SIMD-ready**: Clean interfaces for vectorization
- **Lock-free reads**: Atomic operations for consistency
- **Memory-mapped**: OS handles paging optimally

## Migration Path

Existing code can migrate incrementally:

```cpp
// Compatibility wrapper
namespace maph {
    class Maph {
        v3::maph impl_;
    public:
        // Forward old API to new implementation
        auto get(JsonView key) {
            return impl_.get(key).transform([](auto sv) {
                return std::optional{sv};
            }).value_or(std::nullopt);
        }
    };
}
```

## Conclusion

The refactored maph v3 demonstrates that high performance and elegant design are not mutually exclusive. By following fundamental principles of software architecture:

1. **Separation of Concerns**: Each component has one job
2. **Composability**: Components combine orthogonally
3. **Strong Typing**: The type system prevents errors
4. **Functional Design**: Immutability and pure functions
5. **RAII**: Automatic resource management

The result is a library that is:
- **Powerful**: More capable than the original
- **Simple**: Each piece is understandable in isolation
- **Composable**: New capabilities emerge from composition
- **Elegant**: A joy to use and extend
- **Fast**: Zero-cost abstractions maintain performance

This is what great C++ looks like - leveraging the language's power to create abstractions that are both beautiful and efficient.