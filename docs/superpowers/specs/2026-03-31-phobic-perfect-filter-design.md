# PHOBIC + Perfect Filter + Clean PHF Concept

## Problem

maph's existing perfect hash algorithms (RecSplit, CHD, BBHash, FCH, PTHash) bake fingerprint verification and overflow handling directly into the hasher class. This conflates two concerns: the hash function itself and membership verification. It inflates space (50-100 bits/key), prevents composability, and forces every new algorithm to reimplement serialization of fingerprints, overflow arrays, and SIMD lookup.

We need:
1. A clean PHF concept that defines what a perfect hash function *is* (a bijection from keys to slots, nothing more)
2. PHOBIC (2024, Lehmann & Walzer) as the first algorithm under this concept, achieving ~2 bits/key with fast queries
3. A composition layer (`perfect_filter`) that adds approximate membership verification on top of any PHF

## Goals

1. Define a `perfect_hash_function` concept for both minimal and non-minimal PHFs
2. Implement PHOBIC (2024, Lehmann & Walzer) as the first algorithm under the new concept
3. Implement `perfect_filter<PHF, FPBits>` that composes any PHF with packed fingerprints
4. Relax `packed_fingerprint_array` to support non-power-of-2 widths (e.g., 10 bits)
5. Remove `xor_filter` and `ribbon_filter` from `membership.hpp` (future separate project)
6. Keep existing hashers working unchanged

## Non-Goals

- Replacing existing hashers (RecSplit, CHD, BBHash, FCH, PTHash) with the new concept
- `perfect_map` (value storage layer; follow-up project)
- Python bindings (follow-up project)
- Type-erased PHF wrapper (follow-up; design is known, just not this spec)
- New separate library for membership filters (xor, ribbon, bloom; future project)

## Architecture

### Layer 1: PHF Concept (`include/maph/phf_concept.hpp`)

```cpp
template<typename P>
concept perfect_hash_function = requires(P p, std::string_view key) {
    { p.slot_for(key) }    -> std::convertible_to<slot_index>;  // always succeeds for build keys
    { p.num_keys() }       -> std::convertible_to<size_t>;      // n
    { p.range_size() }     -> std::convertible_to<size_t>;      // m (>= n)
    { p.bits_per_key() }   -> std::convertible_to<double>;      // space of hash structure
    { p.memory_bytes() }   -> std::convertible_to<size_t>;
    { p.serialize() }      -> std::convertible_to<std::vector<std::byte>>;
};
```

Key properties:
- `slot_for()` returns `slot_index` (not optional). For keys in the build set, the result is a unique slot in [0, range_size()). For keys not in the build set, the result is arbitrary (undefined but valid index in [0, range_size())).
- `range_size() >= num_keys()`. When equal, the PHF is minimal.
- No fingerprints, no overflow, no membership checking. Pure function.

Builder concept:

```cpp
template<typename B, typename PHF>
concept phf_builder = requires(B b, std::string_view key, std::vector<std::string> keys) {
    { b.add(key) }       -> std::same_as<B&>;
    { b.add_all(keys) }  -> std::same_as<B&>;
    { b.build() }        -> std::same_as<result<PHF>>;
};
```

### Layer 2: PHOBIC Algorithm (`include/maph/phobic.hpp`)

PHOBIC (Packed Highly Optimized Bijective Indirect Compact) from Lehmann & Walzer, 2024.

**Algorithm overview (pragmatic version):**

1. **Partition** keys into buckets using a primary hash.
2. **Pilot search** per bucket: for each bucket, find a small integer "pilot" value such that hashing each key with the pilot produces a distinct slot not claimed by any previous bucket. This is a brute-force search per bucket, but buckets are small (average ~5 keys) so it's fast.
3. **Store pilots** in a compact array. Typical pilots are small (0-255), so 8 bits per bucket suffices for most. Larger pilots use an overflow structure.
4. **Query**: hash key to bucket, read pilot, hash key with pilot to get slot. Two hash computations + one array read.

**Space**: ~2.0-2.5 bits/key depending on parameters.
**Query**: ~15-25ns (two hash computations + one pilot array read).
**Build**: O(n) expected time. Fast at scale (seconds for 10M+ keys).

Template parameters:
- `BucketSize` (average keys per bucket, default 5): larger = smaller pilot array but slower build

**Interface:**

```cpp
template<size_t BucketSize = 5>
class phobic_phf {
public:
    class builder;

    slot_index slot_for(std::string_view key) const noexcept;
    size_t num_keys() const noexcept;
    size_t range_size() const noexcept;
    double bits_per_key() const noexcept;
    size_t memory_bytes() const noexcept;

    std::vector<std::byte> serialize() const;
    static result<phobic_phf> deserialize(std::span<const std::byte> data);
};
```

Builder supports:
- `add(key)`, `add_all(keys)`, `build()` (satisfies `phf_builder`)
- `with_seed(uint64_t)` for reproducibility
- `with_alpha(double)` to control range_size/num_keys ratio (1.0 = minimal, >1.0 = non-minimal for faster build/query)

### Layer 3: Perfect Filter (`include/maph/perfect_filter.hpp`)

Composes any `perfect_hash_function` with `packed_fingerprint_array` to provide approximate membership testing and guarded slot access.

```cpp
template<typename PHF, unsigned FPBits = 16>
class perfect_filter {
    PHF phf_;
    packed_fingerprint_array<FPBits> fps_;

public:
    // Construction from keys + pre-built PHF
    static perfect_filter build(PHF phf, const std::vector<std::string>& keys);

    // Approximate membership test
    bool contains(std::string_view key) const noexcept;

    // Guarded slot access (nullopt if fingerprint mismatch)
    std::optional<slot_index> slot_for(std::string_view key) const noexcept;

    // Access to underlying PHF (for users who know their keys are valid)
    const PHF& phf() const noexcept;

    // Delegation
    size_t num_keys() const noexcept;
    size_t range_size() const noexcept;

    // Serialization (PHF + fingerprint array as one blob)
    std::vector<std::byte> serialize() const;
    static result<perfect_filter> deserialize(std::span<const std::byte> data);
};
```

Query flow:
1. Compute `slot = phf_.slot_for(key)` (always returns a slot)
2. Check `fps_.verify(key, slot.value)` (fingerprint match?)
3. If match: return slot. If not: return nullopt.

FP rate: 2^-FPBits for non-member keys.

### Changes to membership.hpp

- `packed_fingerprint_array`: relax `requires` clause from `(FPBits == 8 || FPBits == 16 || FPBits == 32)` to `(FPBits >= 1 && FPBits <= 32)`. The bit-packing code already handles arbitrary widths.
- Remove `xor_filter` class (move to future separate project)
- Remove `ribbon_filter` class (move to future separate project)
- Remove `fingerprint_verifier` class (superseded by `perfect_filter`)
- Keep `membership_fingerprint()` hash function
- Keep `packed_fingerprint_array`

## File Layout

```
include/maph/
    phf_concept.hpp        -- perfect_hash_function and phf_builder concepts
    phobic.hpp             -- PHOBIC algorithm
    perfect_filter.hpp     -- perfect_filter composition
    membership.hpp         -- packed_fingerprint_array only (xor/ribbon removed)
    core.hpp               -- unchanged
    hashers.hpp            -- unchanged
    hashers_perfect.hpp    -- unchanged (existing algorithms keep working)
    storage.hpp            -- unchanged
    table.hpp              -- unchanged
    optimization.hpp       -- unchanged
    maph.hpp               -- unchanged
```

New test files:

```
tests/v3/
    test_phf_concept.cpp       -- concept satisfaction checks
    test_phobic.cpp            -- PHOBIC algorithm tests
    test_perfect_filter.cpp    -- perfect_filter composition tests
```

New benchmark:

```
benchmarks/
    bench_phobic.cpp           -- PHOBIC vs existing algorithms
```

## PHOBIC Algorithm Details

### Construction

1. **Hash all keys** to 128-bit values (two 64-bit hashes). Use the existing `membership_fingerprint` for one, a second independent hash for the other.

2. **Assign keys to buckets**: `bucket_id = hash_high >> (64 - log2(num_buckets))`. Number of buckets = `ceil(n / BucketSize)`.

3. **For each bucket, find a pilot** (uint8_t, typically 0-255):
   - For pilot value p = 0, 1, 2, ...:
     - For each key in bucket: compute `slot = hash(key, p) % range_size`
     - If all slots are distinct AND none already claimed: accept pilot p, mark slots as claimed
   - If no pilot found in 0-255, use a fallback with wider pilot (uint16_t) and store in overflow

4. **Store pilots** in a compact array (1 byte per bucket for typical cases).

5. **Build slot-to-bucket reverse mapping** for efficient range verification.

### Query

```
bucket_id = hash_high(key) >> shift
pilot = pilots[bucket_id]
slot = hash(key, pilot) % range_size
return slot_index{slot}
```

Two hash computations, one byte read from the pilot array, one modulo. Expected ~15-25ns.

### Parameters

- `BucketSize` (default 5): keys per bucket on average. Larger buckets = fewer pilots to store (less space) but harder pilot search (slower build). 5 is the sweet spot from the paper.
- `alpha` (default 1.0): range_size = ceil(n * alpha). At 1.0, the PHF is minimal. At 1.01-1.05, build is faster because there are spare slots.

### Space Analysis

- Pilot array: 8 bits per bucket = 8/BucketSize bits/key = ~1.6 bits/key at BucketSize=5
- Bucket metadata: ~0.2-0.4 bits/key
- Total: ~2.0-2.5 bits/key (without fingerprints)
- With 16-bit fingerprints via perfect_filter: ~18-18.5 bits/key
- With 8-bit fingerprints: ~10-10.5 bits/key

### Serialization Format

```
Header:
    magic: uint32 (0x4D415048 "MAPH")
    version: uint32
    algorithm_id: uint32 (6 = PHOBIC)

Body:
    seed: uint64
    num_keys: uint64
    range_size: uint64
    num_buckets: uint64
    bucket_size: uint64
    pilots: uint8[num_buckets]
    overflow_count: uint64
    overflow_bucket_ids: uint64[overflow_count]
    overflow_pilots: uint16[overflow_count]
```

## Testing Strategy

### PHOBIC Tests (`test_phobic.cpp`)

1. **Concept satisfaction**: static_assert that phobic_phf satisfies perfect_hash_function
2. **Bijectivity**: build from key set, verify all keys get distinct slots in [0, range_size)
3. **Small key sets**: 1, 3, 10, 100 keys
4. **Medium key sets**: 1K, 10K, 100K keys
5. **Non-minimal mode**: alpha=1.05, verify range_size > num_keys, verify bijectivity still holds
6. **Serialization round-trip**: serialize, deserialize, verify same slot assignments
7. **Deterministic builds**: same keys + same seed = same slot assignments
8. **Space efficiency**: verify bits_per_key < 3.0 for 10K+ keys

### Perfect Filter Tests (`test_perfect_filter.cpp`)

1. **Known keys accepted**: all build keys return slots via slot_for
2. **Unknown keys rejected**: FP rate within statistical bounds
3. **contains() matches slot_for()**: contains returns true iff slot_for returns non-nullopt
4. **Serialization round-trip**: serialize, deserialize, verify same behavior
5. **PHF access**: verify phf() returns the underlying PHF, direct slot_for works
6. **Multiple FP widths**: test with 8, 10, 16 bits

### Concept Tests (`test_phf_concept.cpp`)

1. **phobic_phf satisfies perfect_hash_function**: static_assert
2. **Mock PHF satisfies perfect_hash_function**: verify concept is not over-constrained

## Benchmarks (`bench_phobic.cpp`)

Compare PHOBIC against existing RecSplit, CHD, BBHash at:
- Key counts: 10K, 100K, 1M, 10M
- Metrics: build time, query latency (median, p99), bits/key, memory bytes
- Include perfect_filter<phobic_phf, 16> for the "with fingerprints" comparison

## Success Criteria

- PHOBIC achieves < 3.0 bits/key for 10K+ keys (without fingerprints)
- PHOBIC query latency < 30ns median at 1M keys
- PHOBIC builds 10M keys in < 10 seconds
- perfect_filter FP rate matches 2^-FPBits within statistical bounds
- All existing tests continue to pass
- packed_fingerprint_array works with 10-bit width
