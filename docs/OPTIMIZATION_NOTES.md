# Optimization Notes

## PHOBIC Build Time

PHOBIC build is slow (~248s for 1M keys) due to brute-force pilot search per bucket. Opportunities:

1. **Parallel pilot search**: buckets that don't share candidate slots can be processed concurrently. Sort buckets by size descending (already done), then process in parallel with a shared atomic bitset for slot occupancy.
2. **Better hash independence**: the current wyhash-style dual hash works but may cause unnecessary pilot retries. Tabulation hashing or multiply-shift could reduce collisions during pilot search.
3. **Bucket ordering**: currently sorts by size descending. Could also try random ordering or greedy (process buckets whose candidate slots have least contention).
4. **Compact occupied bitset**: replace `vector<bool>` with a 64-bit word array for cache-friendly occupancy checks during pilot search.
5. **Early termination in pilot search**: once a pilot produces a collision, skip remaining keys in the bucket immediately (already done). Could also cache partial hash results across pilot attempts.

## Existing Algorithm Refactoring

The existing algorithms (RecSplit, CHD, BBHash, FCH, PTHash) bake 64-bit fingerprints and overflow handling into the hasher class. This inflates their reported bits/key and prevents fair comparison with PHOBIC.

To bring them under the clean PHF concept (`perfect_hash_function`):

### RecSplit
- Core hash structure is ~96 bits/key (excluding 64-bit fingerprints + overflow)
- Refactor: extract the bucket/split structure as a pure PHF, move fingerprints to `perfect_filter`
- The `slot_for()` return type changes from `optional<slot_index>` to `slot_index`
- Overflow handling moves out (the pure PHF either places a key or doesn't; builds can fail)

### BBHash
- Core hash structure is ~6 bits/key (the multi-level bitset + rank structure)
- This is already very compact. Refactoring under the PHF concept would make it competitive with PHOBIC on space.
- The rank-based query is O(1) and fast (~12ns in the existing benchmarks)

### CHD
- Core hash structure is ~134 bits/key (displacement table is large)
- Less competitive on space, but build is fast and well-tested

### FCH
- Simple algorithm, educational value
- Not competitive on space or speed

### PTHash
- Limited to small key sets (<100 keys) in current implementation
- Not worth refactoring until the scale limitation is addressed

### Priority order for refactoring
1. **BBHash** (already compact, fastest queries, most to gain from clean separation)
2. **RecSplit** (upgrade path from current simplified impl to SIMDRecSplit)
3. **CHD** (proven reliability, but large displacement table)

## Benchmark Fairness

The current `bench_phobic.cpp` benchmark is not a fair comparison:
- PHOBIC reports pure hash structure bits/key (no fingerprints)
- Existing algorithms report total bits/key (including 64-bit fingerprints + overflow)

A fair comparison requires either:
- Refactoring existing algorithms under the PHF concept and wrapping with `perfect_filter<16>`
- Or subtracting the known fingerprint overhead from existing algorithms' reported bits/key (approximate, since overflow structures also add overhead)

## Query Latency Notes

The 451ns median for PHOBIC at 1M keys includes `chrono::now()` measurement overhead (~20-30ns per call). Batch measurement (time N queries, divide) would give more accurate per-query numbers. The existing algorithms show similar measurement overhead (750-812ns). The relative comparison is meaningful; the absolute numbers are inflated.
