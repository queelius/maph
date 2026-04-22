# maph

**Modern Approximate Perfect Hashing.** A C++23 research playground for perfect hash functions and related approximate data structures. Concept-driven: a concept defines the contract, algorithms are interchangeable implementations, benchmarks compare them.

Not a production library. For a production Python package using one of these algorithms (PHOBIC), see [phobic](https://github.com/queelius/phobic).

## Why

Perfect hash function design is a tradeoff space: bits per key, query latency, construction cost, and false positive rate (when composed with membership verification). New algorithms appear every few years. This repo exists to prototype, compare, and evaluate them against a crystallized concept so the comparisons are apples to apples.

Every perfect hash function satisfies the same concept (`slot_for`, `num_keys`, `range_size`, `serialize`), so adding one is a single header file plus a benchmark entry.

## Install

Header-only. Requires C++23 (GCC 13+ or Clang 16+).

```bash
git clone https://github.com/queelius/maph
cd maph && mkdir build && cd build
cmake .. -DBUILD_TESTS=ON -DBUILD_BENCHMARKS=ON
make -j$(nproc)
ctest --output-on-failure
```

## Quick look

```cpp
#include <maph/algorithms/phobic.hpp>
#include <maph/composition/perfect_filter.hpp>

// Pure PHF: keys to unique slots.
auto phf = maph::phobic5::builder{}.add_all(keys).build().value();
auto slot = phf.slot_for("alice");   // 0..range_size()

// PHF plus 16-bit fingerprint: approximate membership + slot access.
auto pf = maph::perfect_filter<maph::phobic5, 16>::build(std::move(phf), keys);
if (auto s = pf.slot_for("alice")) { /* in set, unique slot */ }
pf.contains("alice");   // true
pf.contains("mallory"); // almost certainly false (FP rate 1/65536)
```

## What's inside

```
include/maph/
    core.hpp                              strong types, error, result<T>
    concepts/                             contracts
        perfect_hash_function.hpp
        membership_oracle.hpp
        approximate_map.hpp
    detail/                               shared helpers
        serialization.hpp, hash.hpp, fingerprint_hash.hpp
    algorithms/                           perfect hash functions
        phobic.hpp, recsplit.hpp, chd.hpp, bbhash.hpp, fch.hpp, pthash.hpp
    filters/                              membership oracles
        packed_fingerprint.hpp, xor_filter.hpp, ribbon_filter.hpp
    composition/
        perfect_filter.hpp                PHF + packed fingerprint
```

## Concept vs implementation

The `perfect_hash_function` concept says: given a key in the build set, return a unique slot. Nothing about membership, nothing about fingerprints, nothing about overflow. Membership verification is a separate concern, answered by `membership_oracle`. Their composition is the `approximate_map`.

This separation is the library's central design choice. It makes comparisons clean: you can report the pure PHF bits/key, or add a 16-bit fingerprint and report bits/key with a 1/65536 FP rate, or try a different oracle entirely.

## Space comparison: perfect filter vs Bloom

A perfect filter (PHF + k-bit fingerprints) uses `c + k` bits per key, where `c` is the PHF's bits/key and `k = log2(1/eps)` for target FP rate `eps`. A Bloom filter uses `1.44 * k` bits/key.

The perfect filter beats Bloom whenever `eps < 2^-(c/0.44)`. At PHOBIC's ~2.7 bits/key, that crossover is at `eps ≈ 1.4%`. For `eps < 1%`, the perfect filter is strictly better, and the advantage grows as `eps` shrinks because Bloom pays 1.44 bits per bit of FP precision while the perfect filter pays exactly 1.

## Benchmarks

`./benchmarks/bench_phobic 10000 100000 1000000` prints a TSV with build time, query latency, bits/key, and memory for every algorithm (pure and composed with `perfect_filter<_, 16>`).

## Adding an algorithm

One file in `include/maph/algorithms/`. Satisfy `perfect_hash_function`. Add `static_assert(perfect_hash_function<your_type>);`. Add tests modeled on `tests/v3/test_phobic.cpp`. Add a benchmark entry in `bench_phobic.cpp`. Done.

See `CLAUDE.md` for a more detailed developer guide, `docs/OPTIMIZATION_NOTES.md` for what's left to improve.

## License

MIT.
