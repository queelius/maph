# Repository Guidelines

## Project Structure & Module Organization
- `include/maph`: Public headers and core abstractions for perfect-hash storage, builders, and hashers.
- `src`: CLI and service entrypoints (e.g., `maph_cli.cpp`) plus supporting implementation files.
- `integrations/rest_api`: REST server sources, configs, and Python client (`python/maph_client`).
- `benchmarks` and `examples`: Performance suites and minimal usage samples; mirror core API patterns.
- `tests` and `tests/v3`: Catch2-based unit/integration suites; build outputs land under your chosen build directory (e.g., `build/tests/...`).
- `docs`: Architecture notes, API docs, and technical reports; update when behavior or interfaces change.

## Build, Test, and Development Commands
```bash
# Configure (toggle options: BUILD_TESTS, BUILD_REST_API, BUILD_EXAMPLES, BUILD_DOCS)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DBUILD_REST_API=ON -DBUILD_EXAMPLES=ON

# Compile
cmake --build build -j"$(nproc)"

# Unit and integration tests (from build dir)
ctest --test-dir build --output-on-failure
# Or run the focused suite harness (from the build dir so binaries are in ./tests)
../tests/run_test_suite.sh
```
For quick iteration, use `cmake --build build --target <name>` to rebuild only a binary (e.g., `maph_cli`, `test_maph`).

## Coding Style & Naming Conventions
- C++17 codebase; indent with 4 spaces; keep lines ≤100 chars; prefer `auto` when the type is obvious.
- Names: classes/types `PascalCase`, functions `snake_case`, constants `UPPER_SNAKE_CASE`, members `member_name_`, template params `PascalCase`.
- Favor RAII, `const`, and explicit ownership (`std::unique_ptr`). Document public APIs in headers. Avoid magic numbers—extract named constants.

## Testing Guidelines
- Framework: Catch2. Tag tests (e.g., `[core]`, `[storage]`, `[perfect_hash]`) so they can be filtered (`ctest -R core` or `./tests/test_maph "[core]"` from the build dir).
- Aim for >90% coverage on new/changed code; cover edge cases and error paths. Add benchmarks for performance-sensitive changes.

## Commit & Pull Request Guidelines
- Use conventional commits (`feat:`, `fix:`, `docs:`, `test:`, `refactor:`, `perf:`, `chore:`). Keep messages imperative and scoped to one logical change.
- PRs should: describe intent and behavior changes; link issues; note testing performed; include perf notes if relevant; and add screenshots/CLI examples for user-facing updates.
- Ensure CI parity locally: rebuild with tests enabled and run `ctest` (or the suite script) before requesting review.
