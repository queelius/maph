# Contributing to maph

Thank you for your interest in contributing to maph! This document provides guidelines and instructions for contributing to the project.

## Code of Conduct

By participating in this project, you agree to maintain a respectful and inclusive environment for all contributors.

## How to Contribute

### Reporting Issues

- Check existing issues before creating a new one
- Use descriptive titles and provide detailed descriptions
- Include steps to reproduce bugs
- Specify your environment (OS, compiler, versions)

### Suggesting Features

- Open an issue with the "enhancement" label
- Clearly describe the feature and its use case
- Discuss the design before implementing

### Submitting Pull Requests

1. **Fork and Clone**
   ```bash
   git clone https://github.com/yourusername/maph.git
   cd maph
   ```

2. **Create a Feature Branch**
   ```bash
   git checkout -b feature/your-feature-name
   ```

3. **Make Your Changes**
   - Follow the coding standards (see below)
   - Add tests for new functionality
   - Update documentation as needed

4. **Test Your Changes**
   ```bash
   mkdir build && cd build
   cmake .. -DBUILD_TESTS=ON
   make -j
   ctest --verbose
   ```

5. **Commit Your Changes**
   ```bash
   git commit -m "feat: add new feature X"
   ```
   Use conventional commit messages:
   - `feat:` for new features
   - `fix:` for bug fixes
   - `docs:` for documentation changes
   - `test:` for test additions/changes
   - `refactor:` for code refactoring
   - `perf:` for performance improvements
   - `chore:` for maintenance tasks

6. **Push and Create PR**
   ```bash
   git push origin feature/your-feature-name
   ```
   Then create a pull request on GitHub.

## Coding Standards

### C++ Style Guide

- **C++ Version**: Use C++20 (with C++23 features like std::expected)
- **Naming Conventions**:
  - Classes: `PascalCase`
  - Functions/Methods: `snake_case`
  - Constants: `UPPER_SNAKE_CASE`
  - Member variables: `member_name_`
  - Template parameters: `PascalCase`

- **Formatting**:
  - Indent with 4 spaces
  - Max line length: 100 characters
  - Braces on same line for functions
  - Use `auto` when type is obvious

- **Best Practices**:
  - Use RAII for resource management
  - Prefer `std::unique_ptr` over raw pointers
  - Use `const` wherever possible
  - Document public APIs with comments
  - Avoid magic numbers - use named constants

### Example Code

```cpp
namespace maph {

template <typename StorageType>
class ExampleClass {
public:
    static constexpr size_t DEFAULT_SIZE = 1024;
    
    explicit ExampleClass(size_t size = DEFAULT_SIZE) 
        : size_(size) {
        // Constructor implementation
    }
    
    // Public method with documentation
    // Returns true if operation successful
    bool process_data(const JsonView& data) const {
        if (data.empty()) {
            return false;
        }
        // Processing logic
        return true;
    }
    
private:
    size_t size_;
};

} // namespace maph
```

## Testing Guidelines

- Write tests for all new functionality
- Aim for >90% code coverage
- Use descriptive test names
- Test edge cases and error conditions
- Performance benchmarks for critical paths

### Test Structure

```cpp
TEST_CASE("Feature description", "[component][tag]") {
    SECTION("Specific behavior") {
        // Arrange
        auto obj = create_test_object();
        
        // Act
        auto result = obj.perform_action();
        
        // Assert
        REQUIRE(result == expected_value);
    }
}
```

## Documentation

- Update README.md for user-facing changes
- Document new APIs in header files
- Update CHANGELOG.md following Keep a Changelog format
- Include examples for new features

## Performance Considerations

maph is a high-performance library. When contributing:

- Profile before and after changes
- Avoid unnecessary allocations
- Use SIMD operations where beneficial
- Maintain O(1) complexity for core operations
- Consider cache-friendliness in data structures

## Review Process

1. All PRs require at least one review
2. CI must pass (tests, formatting, static analysis)
3. Performance benchmarks must not regress
4. Documentation must be updated

## Development Setup

### Prerequisites

- C++23 compatible compiler (GCC 13+, Clang 16+, MSVC 2022+)
- CMake 3.14+
- Optional: clang-format, clang-tidy, valgrind

### Recommended Tools

- **Formatting**: `clang-format` with project .clang-format
- **Static Analysis**: `clang-tidy`
- **Memory Checking**: `valgrind` or AddressSanitizer
- **Profiling**: `perf`, `vtune`, or `instruments`

### Building with Sanitizers

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug \
         -DCMAKE_CXX_FLAGS="-fsanitize=address -fsanitize=undefined"
make -j
```

## Areas for Contribution

Current areas where we especially welcome contributions:

- **Perfect Hash Functions**: Alternative implementations
- **Custom Decoders**: Specialized use cases
- **Language Bindings**: Python, Rust, Go, Java
- **Performance**: SIMD optimizations, cache improvements
- **Documentation**: Tutorials, examples, benchmarks
- **Testing**: Fuzzing, stress tests, coverage

## Questions?

Feel free to:
- Open an issue for questions
- Join discussions in existing issues
- Reach out to maintainers

Thank you for contributing to maph!