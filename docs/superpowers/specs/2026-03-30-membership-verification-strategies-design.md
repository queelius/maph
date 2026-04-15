# Membership Verification Strategies for Perfect Hash Functions

## Problem

maph's perfect hash algorithms store 64-bit fingerprints per key to reject unknown keys. This adds 64 bits/key of overhead on top of the hash structure itself (typically 2-4 bits/key), inflating total space to 50-100 bits/key. The theoretical minimum for the hash structure alone is ~1.44 bits/key. We need to reduce the membership verification overhead by ~10x while preserving the ability to reject unknown keys.

## Goals

1. Implement four membership verification strategies with different space/speed/complexity tradeoffs
2. Benchmark all four at scale (1M to 100M keys) measuring bits/key, build time, query latency, and false positive rate
3. Identify the best default strategy for integration into production hashers
4. Establish a configurable fingerprint width so users can tune the space/FP-rate tradeoff

## Non-Goals

- Replacing the existing perfect hash algorithms (that's a follow-up project)
- Integrating the winner into production hashers yet (benchmark first, integrate second)
- GPU or distributed construction

## Strategies

### Strategy 1: Compact Packed Fingerprint Array

**Concept:** Once a perfect hash maps n keys to slots 0..n-1, store a tightly packed bit array where `fingerprint[slot] = truncate(hash(key), k)` for k-bit fingerprints.

**Space:** Exactly k bits/key. No structural overhead.

**Query:** Compute perfect hash slot, extract k-bit fingerprint from packed array, compare against truncated hash of query key.

**Build:** O(n). For each key, compute perfect hash slot, compute fingerprint, pack into array.

**FP rate:** 2^-k. At k=8: 1/256 (0.39%). At k=16: 1/65536 (0.0015%). At k=32: ~2.3e-10.

**Implementation notes:**
- Template on `FingerprintBits` (8, 16, 32)
- Use `std::vector<uint64_t>` as backing store, manual bit packing/unpacking
- Bit extraction must handle fingerprints that straddle word boundaries

### Strategy 2: Xor Filter Sidecar

**Concept:** Attach a separate xor filter (Dietzfelbinger & Walzer, 2019) as a membership oracle. The perfect hash structure stores no fingerprints at all.

**Space:** ~1.23 * k bits/element for k-bit fingerprints. At k=8: ~9.84 bits/key. At k=16: ~19.68 bits/key.

**Query:** 3 memory accesses (hash to 3 positions, XOR the values, compare against fingerprint). Independent of the perfect hash lookup.

**Build:** O(n) expected time. "Peeling" algorithm on a random 3-hypergraph.

**FP rate:** 2^-k (same as Strategy 1).

**Implementation notes:**
- Standalone `xor_filter<FingerprintBits>` class
- Three hash functions derived from a single 128-bit hash (split into segments)
- Build uses the "peeling" algorithm: repeatedly remove degree-1 vertices from the hypergraph
- If peeling fails (happens ~3% of the time for 3-partite xor filters), retry with a new seed
- Binary fuse filter variant achieves ~1.125 * k bits/key but requires sorted construction

### Strategy 3: Ribbon Retrieval

**Concept:** A ribbon filter is a retrieval data structure: given a set S and a function f: S -> {0,1}^r, it constructs a compact structure that, for any x in S, returns f(x), using approximately r bits per element. We store a short fingerprint as the retrieved value.

**Space:** ~r * (1 + epsilon) bits/key where epsilon is small (~1-5%). At r=8: ~8.2 bits/key. At r=16: ~16.4 bits/key.

**Query:** Hash the key to a starting row and a coefficient vector. Compute a dot product (XOR chain) with the stored solution matrix. Single cache line in practice.

**Build:** O(n) with Gaussian elimination on a banded system. Band width w (typically 64 for 64-bit words). Each key generates a row with w random bits starting at a pseudorandom position.

**FP rate:** 2^-r.

**Implementation notes:**
- Homogeneous ribbon with width w=64 (one machine word)
- Solution stored as vector of r-bit values
- Construction: sort rows by starting position, forward elimination, back substitution
- If a row cannot be eliminated (rare), bump it to an overflow list

### Strategy 4: Configurable Width Wrapper

**Concept:** A policy layer that wraps Strategy 1 (compact packed array) with a template parameter `FingerprintBits`. Default 16. Setting to 0 disables verification entirely.

**Space:** FingerprintBits bits/key (0 if disabled).

**This is the integration interface**, not a separate data structure. It demonstrates how any of the strategies would be wired into a perfect hasher.

**Implementation notes:**
- `template<unsigned FingerprintBits = 16> class fingerprint_verifier`
- Specialization for `FingerprintBits = 0` that always returns true
- Methods: `build(keys, slot_for_fn)`, `verify(key, slot)`, `serialize()`, `deserialize()`
- Can later be swapped to use Strategy 2 or 3 as the backing implementation

## Benchmark Design

### Key Generation

- Random 16-byte string keys (uniform random bytes)
- Pre-generated and stored in a vector before benchmarking
- Same key set used across all strategies for fair comparison

### Benchmark Matrix

| Parameter | Values |
|-----------|--------|
| Key count | 1M, 10M, 50M, 100M |
| Fingerprint width | 8, 16, 32 bits |
| Strategy | packed_array, xor_filter, ribbon, configurable(packed) |

### Metrics

1. **Bits per key** (total structure size / key count)
2. **Build time** (wall clock, single-threaded)
3. **Query latency, known keys** (median and p99 over 1M random lookups from the build set)
4. **Query latency, unknown keys** (median and p99 over 1M random strings not in the build set)
5. **False positive rate** (query 10M random strings not in the build set, count how many are not rejected)

### Output Format

Tab-separated table to stdout, suitable for piping to analysis tools.

### Underlying Perfect Hash

Use RecSplit (the existing `recsplit_hasher<8>`) as the perfect hash for all benchmarks, with `with_threads(4)` for key counts above 1M. The membership strategy is what we're comparing, not the hash function. If RecSplit construction is prohibitively slow at 100M keys, cap at 50M and note the limitation.

### Note on Strategy 4

Strategy 4 (configurable wrapper) wraps Strategy 1 internally. Benchmarking it alongside Strategy 1 verifies the abstraction layer adds no measurable overhead. At runtime they should be identical.

## File Layout

```
include/maph/membership.hpp          -- All four strategies
tests/v3/test_membership.cpp         -- Correctness tests
benchmarks/bench_membership.cpp      -- Comparative benchmark
```

## Correctness Tests

1. **Round-trip:** Build from key set, verify all keys return correct slots
2. **Rejection:** Verify unknown keys are rejected (with expected FP rate tolerance)
3. **Serialization:** Serialize/deserialize round-trip for each strategy
4. **Edge cases:** Empty key set, single key, duplicate keys, very long keys
5. **FP rate validation:** For each width k, verify empirical FP rate is within 3 standard deviations of 2^-k over 100K trials

## Success Criteria

- At least one strategy achieves <20 bits/key total (hash structure + verification) at 10M keys with FP rate < 0.01%
- Query latency for known keys within 2x of the current 64-bit fingerprint approach
- All strategies pass correctness tests
- Benchmark runs to completion at 100M keys without OOM (assuming 64GB available)

## Future Work

- Integrate winning strategy into production perfect hashers (replace 64-bit fingerprint storage)
- Add binary fuse filter variant (better space than xor filter)
- Use the configurable-width approach when implementing new algorithms (ShockHash, SIMDRecSplit, PHOBIC)
