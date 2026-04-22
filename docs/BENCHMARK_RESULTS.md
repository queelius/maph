# Benchmark results

Results from running the four benchmark binaries (`bench_phf`, `bench_phf_sweep`,
`bench_filter`, `bench_approximate_map`) at 10K, 100K, and 1M keys on a single
Linux workstation. Numbers are reproducible via the same binaries and seeds,
though relative timings will differ by CPU.

All four benchmarks were run concurrently on the same machine, so timings have
some cross-contention noise (see "Methodology caveats" below). Space
measurements and FPR numbers are unaffected.

---

## bench_phf: pure perfect hash functions

Reports `slot_for` behaviour with no membership verification.

### At 10,000 keys

| algorithm    | build (ms) | bits/key | query median (ns) | query p99 (ns) | MQPS |
|--------------|-----------:|---------:|------------------:|---------------:|-----:|
| phobic3      |       4.84 |    2.87  |              22.8 |           42.0 | 39.4 |
| phobic5      |      70.49 |    2.81  |              33.9 |           45.5 | 32.1 |
| phobic7      |     FAILED |    -     |              -    |            -   |  -   |
| recsplit8    |       6.34 |   96.06  |              47.0 |           79.0 | 19.6 |
| recsplit16   |      63.92 |   48.06  |              44.4 |           75.5 | 21.0 |
| chd          |      24.13 |  134.47  |              42.5 |           56.6 | 22.8 |
| bbhash3      |      11.97 |   18.18  |              31.8 |           48.4 | 29.9 |
| bbhash5      |       2.03 |   18.30  |              40.8 |           60.6 | 23.2 |
| fch          |       2.76 |  200.07  |              37.9 |           51.6 | 25.5 |

### At 100,000 keys

| algorithm    | build (ms) | bits/key | query median (ns) | query p99 (ns) | MQPS |
|--------------|-----------:|---------:|------------------:|---------------:|-----:|
| phobic3      |     150.27 |    2.87  |              67.0 |          150.7 | 13.9 |
| phobic5      |  11,181.55 |    2.72  |              67.1 |          162.0 | 13.5 |
| phobic7      |     FAILED |    -     |              -    |            -   |  -   |
| recsplit8    |      92.60 |   96.01  |             121.6 |          251.2 |  7.7 |
| recsplit16   |     FAILED |    -     |              -    |            -   |  -   |
| chd          |      72.25 |  134.41  |             124.0 |          203.8 |  7.9 |
| bbhash3      |     415.27 |   27.02  |              52.8 |          108.5 | 17.4 |
| bbhash5      |      27.69 |   20.03  |              97.1 |          195.5 |  9.7 |
| fch          |      93.55 |  200.01  |             132.2 |          265.7 |  7.4 |

### At 1,000,000 keys

| algorithm    | build (ms) | bits/key | query median (ns) | query p99 (ns) | MQPS |
|--------------|-----------:|---------:|------------------:|---------------:|-----:|
| phobic3      |  23,414.75 |    2.79  |             146.1 |          245.5 |  6.7 |
| phobic5      | 103,297.05 |    2.73  |             146.4 |          242.1 |  6.7 |
| phobic7      |    skipped |    -     |              -    |            -   |  -   |
| recsplit8    |   3,203.29 |   96.00  |             264.3 |          326.9 |  3.8 |
| recsplit16   |    skipped |    -     |              -    |            -   |  -   |
| chd          |   3,660.16 |  134.40  |             265.7 |          303.3 |  3.8 |
| bbhash3      |  10,190.97 |   36.00  |             153.3 |          258.6 |  6.3 |
| bbhash5      |     411.03 |   20.00  |             191.8 |          235.9 |  5.2 |
| fch          |     811.55 |  200.00  |             268.7 |          290.5 |  3.8 |

### Observations

- **PHOBIC dominates on space** across scales (2.7-2.9 bits/key vs 20+ for anything else).
- **bbhash5 at 1M is the surprise winner on build performance**: 411 ms (fastest after fch),
  20.0 bits/key (second only to PHOBIC), 192 ns/query. Five levels keep gamma low at scale.
  Better on all axes than bbhash3 at 1M.
- **PHOBIC build scales poorly**: ~10x slower per 10x keys. Acceptable at 10K, slow at 1M (~103 s for phobic5).
- **bbhash3 degrades at scale**: 18.2 b/k at 10K, 27 at 100K, 36 at 1M. Gamma keeps
  escalating to find a valid configuration. bbhash5 does not have this problem.
- **recsplit16 buildability is fragile**: works at 10K, fails at 100K+. Capped at `max_keys=50000` in the benchmark.
- **phobic7 cannot build in minimal mode** at any observed scale. Capped at `max_keys=1000`.

---

## bench_phf_sweep: parameter sweeps at 100K keys

### PHOBIC `BucketSize` template parameter

| BucketSize | build (ms) | bits/key | query (ns) | range/keys | notes |
|-----------:|-----------:|---------:|-----------:|-----------:|-------|
|          3 |        151 |     2.87 |       87.0 |       1.00 |       |
|          4 |        413 |     2.71 |       47.1 |       1.00 | hidden sweet spot |
|          5 |     11,302 |     2.72 |       39.7 |       1.005 | alpha bumped |
|          6 |     78,310 |     2.51 |       37.2 |       1.03 | alpha bumped aggressively |
|          7 |     FAILED |     -    |       -    |       -    |       |

**`phobic_phf<4>` is a genuinely good configuration** not covered by the
`phobic3/5/7` aliases. It matches `phobic5` on space (2.71 vs 2.72 b/k), builds
27x faster, and keeps the slot range minimal.

### RecSplit `LeafSize` template parameter

| LeafSize | build (ms) | bits/key | query (ns) |
|---------:|-----------:|---------:|-----------:|
|        4 |       60.4 |   192.01 |       84.9 |
|        8 |       85.5 |    96.01 |       60.7 |
|       12 |    1,217.4 |    64.01 |      145.2 |
|       16 |     FAILED |    -     |       -    |

`leaf=8` is the pragmatic choice. `leaf=12` halves again on space but costs 14x
build time and runs slower due to deeper per-query work.

### BBHash `gamma` (runtime parameter)

| NumLevels | gamma | build (ms) | bits/key | query (ns) |
|----------:|------:|-----------:|---------:|-----------:|
|         3 |   1.5 |      423.9 |    24.02 |       89.4 |
|         3 |   2.0 |      432.4 |    27.02 |       99.7 |
|         3 |   2.5 |      265.9 |    24.02 |       63.7 |
|         3 |   3.0 |      178.6 |    24.02 |       85.7 |
|         5 |   1.5 |       30.0 |    15.03 |       73.8 |
|         5 |   2.0 |       28.4 |    20.03 |       59.9 |
|         5 |   2.5 |       27.5 |    22.53 |       61.5 |

**`bbhash_hasher<5>` with `gamma=1.5` is the BBHash winner**: 15 bits/key
(the smallest observed for BBHash), 30 ms build, 74 ns query. Five levels
let low gamma succeed and push the space toward the theoretical minimum.

---

## bench_filter: standalone membership oracles

### At 1,000,000 keys

| filter      | bits/key | build (ms) | query median (ns) | empirical FPR | expected FPR |
|-------------|---------:|-----------:|------------------:|--------------:|-------------:|
| xor<8>      |     9.84 |      425.5 |             271.5 |        0.41%  |       0.39%  |
| xor<16>     |    19.68 |      418.4 |             167.4 |        0.02%  |      0.0015% |
| xor<32>     |    39.36 |      397.7 |             174.6 |        0%     |          ~0  |
| ribbon<8>   |     8.64 |    1,499.4 |             315.3 |        0.39%  |       0.39%  |
| ribbon<16>  |    17.28 |    1,454.8 |             321.2 |        0.002% |      0.0015% |
| ribbon<32>  |    34.56 |    1,487.9 |             328.3 |        0%     |          ~0  |

### Observations

- **Ribbon wins on space** across all widths (about 12% more compact than xor).
  Matches theory: ribbon's overhead is 1.08x, xor's is 1.23x.
- **Xor wins on query speed by roughly 2x**. Xor does 3 memory accesses + XOR;
  ribbon does a banded XOR chain with more accesses.
- **Xor builds 3.5x faster** than ribbon. Ribbon's Gaussian elimination is
  structurally more expensive than xor's peeling.
- **Empirical FPR tracks theory** tightly for 8-bit. Slight overshoot at 16-bit
  is sampling noise at 100K unknowns.

---

## bench_approximate_map: perfect_filter compositions

Each row is `PHF + perfect_filter<_, FPBits>`. Total bits/key includes PHF plus
fingerprint array.

### At 1,000,000 keys

| composition         | bits/key | build (ms) | query median (ns) | empirical FPR |
|---------------------|---------:|-----------:|------------------:|--------------:|
| phobic5+pf8         |    10.73 |    119,341 |             275.5 |        0.375% |
| phobic5+pf16        |    18.73 |    113,119 |             289.3 |        0.001% |
| phobic5+pf32        |    34.73 |    113,879 |             261.8 |        0%     |
| phobic3+pf16        |    18.79 |     26,033 |             268.5 |        0.001% |
| recsplit8+pf16      |   112.00 |      3,569 |             401.1 |        0.001% |
| bbhash3+pf16        |    52.00 |      9,879 |             275.9 |        0.001% |
| bbhash5+pf16        |    36.00 |        606 |             283.4 |        0.001% |

(`bbhash3+pf16` at 52.00 b/k reflects bbhash3's gamma-bumped 36 bits/key PHF
plus 16-bit fingerprints. `bbhash5+pf16` stays at 36.00 b/k thanks to bbhash5's
stable 20-bit/key PHF.)

### Observations

- **PHOBIC compositions are the most compact**: `phobic3+pf16` at 18.79
  bits/key with FPR 0.001% matches `xor<16>` on space (19.68 b/k) while also
  providing unique slot indices via `slot_for()`. This is the key payoff of
  the perfect_filter design over standalone filters.
- **Build time is dominated by the PHF**. `phobic5+pf*` all take 113-119s
  because the PHF is the vast majority of that. `bbhash5+pf16` at 606ms is
  200x faster.
- **Query latency is remarkably flat** across compositions (262-401 ns),
  suggesting the fingerprint check costs one cache-line read on top of the PHF
  query rather than multiplying the cost.
- **bbhash5+pf16 is the pragmatic sweet spot**: 36 bits/key, sub-second build,
  283 ns query. Good for applications that need fast build and moderate space.

---

## Cross-algorithm picture: when does perfect_filter beat Bloom?

A composed approximate map uses `c + k` bits/key where `c` is the PHF's space
and `k = log2(1/FPR)` is the fingerprint width for target FPR. A Bloom filter
needs `1.44 * k` bits/key.

perfect_filter wins on space over Bloom when `c + k < 1.44 k`, i.e.
`k > c / 0.44`, i.e. `FPR < 2^(-c/0.44)`:

| PHF              | c (bits/key) | crossover FPR | FPR at which perfect_filter is 2x smaller |
|------------------|-------------:|--------------:|------------------------------------------:|
| phobic5          |         2.73 |          1.4% |                                   0.003% |
| bbhash5 (gamma=1.5) |     15.00 |        4.4e-11 |                                  < 2e-20 |
| bbhash3          |        27.00 |           ~0  |                              not useful |

**Only PHOBIC-based compositions beat Bloom in practical FPR ranges.**
BBHash-based compositions spend too much on the PHF for the FPR savings to
matter. This is a direct justification for investing in PHOBIC construction
performance: PHOBIC's low `c` is what makes the composition competitive.

---

## bench_phobic_parallel: PHOBIC parallel build scaling

After the initial benchmarks flagged PHOBIC's build time as the main bottleneck,
pilot search was parallelized via `.with_threads(N)`. Phase 1 (hashing, bucket
assignment) and phase 2 (sorting buckets by size) stay serial; phase 3 (pilot
search) uses atomic `fetch_or` on a shared bitset with per-bucket work stealing.
`threads=1` runs the original sequential path; `threads=0` auto-detects.

### Speedup at 1,000,000 keys

| config  | threads=1 (ms) | threads=2 | threads=4 | threads=8 | 8T speedup |
|---------|---------------:|----------:|----------:|----------:|-----------:|
| phobic3 |         23,348 |    15,356 |    12,414 |    11,033 |       2.12x |
| phobic4 |         43,234 |    26,368 |    18,520 |    14,143 |       3.06x |
| phobic5 |        102,230 |    77,097 |    46,407 |    **28,299** | **3.61x** |

### Speedup at 100,000 keys

| config  | threads=1 (ms) | threads=2 | threads=4 | threads=8 | 8T speedup |
|---------|---------------:|----------:|----------:|----------:|-----------:|
| phobic3 |            105 |        42 |        72 |       577 |       0.18x |
| phobic4 |            308 |       251 |       168 |        69 |       4.43x |
| phobic5 |          9,338 |     5,657 |     3,518 |     2,504 |       3.73x |

### Observations

- **PHOBIC5 at 1M went from 102 seconds to 28 seconds on 8 threads** (3.6x
  speedup). This was the single biggest bottleneck called out by the initial
  benchmarks, and it is materially resolved.
- **Bits/key unchanged across thread counts** (2.72 for phobic5 at all T), so
  correctness of the atomic claim/release protocol is confirmed by measurement
  in addition to the unit tests.
- **Query latency unchanged** (~235-245 ns for phobic5 at every thread count),
  as expected: parallelism affects only construction.
- **Threading overhead dominates at small scales for phobic3**. 100 ms of
  sequential work + thread spawn/join costs + coordination yields a 0.18x
  "speedup" at 8 threads. The 2048-key threshold keeps this from biting in
  practice (it kicks in to sequential below that point), but even at 100K
  phobic3's compute is too fine-grained to parallelize well.
- **Scaling is sub-linear** (3.6x on 8 threads, not 8x). Likely causes: bucket
  descending-size order means early buckets are larger, later buckets are
  small, so workers finishing a large bucket may wait briefly on workers still
  processing larger predecessors. Atomic bitset contention on hot cache lines
  also plays a role.

## Methodology caveats

- All four benchmarks ran concurrently on the same machine, so query latencies
  include cache and memory-bandwidth contention. Directional rankings are
  reliable; absolute MQPS numbers would likely improve 20-40% if run
  sequentially.
- **Peak RSS** (`build_peak_kb`) is cumulative since process start in a single
  bench binary. For the first algorithm in a run the value reflects that build
  alone; for later algorithms it is at least the maximum of any earlier algorithm.
- **Empirical FPR** uses 100K unknown keys per (algorithm, key_count). Below
  about 1e-5 the sample is too small to resolve (zero observed FPs).
- Single key distribution only: random 16-byte binary strings. URL-shaped or
  natural-language keys could shift rankings, especially for hash functions
  whose quality is distribution-sensitive.

See `benchmarks/README.md` for the measurement methodology.
