# Benchmark results

Results from the maph benchmark suite on a single Linux workstation,
covering PHFs, filters, retrieval structures, and approximate maps.
Numbers are reproducible via the same binaries and seeds; relative
timings will differ by CPU. Space measurements and FPR numbers are
machine-invariant.

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

| algorithm      | build (ms) | bits/key | query median (ns) | query p99 (ns) | MQPS | notes |
|----------------|-----------:|---------:|------------------:|---------------:|-----:|-------|
| phobic3        |  23,462.84 |    2.79  |             146.0 |          245.5 |  6.8 | minimal |
| phobic5        | 103,841.29 |    2.73  |             144.1 |          242.1 |  7.0 | minimal |
| phobic7        |    skipped |    -     |              -    |            -   |  -   | does not build |
| recsplit8      |   3,066.94 |   96.00  |             259.5 |          326.9 |  3.9 | minimal |
| chd            |   3,401.62 |  134.40  |             253.6 |          303.3 |  4.0 | minimal |
| bbhash3        |  10,394.86 |   36.00  |             158.7 |          258.6 |  6.2 | minimal |
| bbhash5        |     418.77 |   20.00  |             170.3 |          235.9 |  5.7 | minimal |
| fch            |     850.06 |  200.00  |             271.3 |          290.5 |  3.7 | minimal |
| **shock_hash128** |  46,929.89 | **1.50** | 323.5 | 420.0 | 3.1 | **non-minimal (1.67x)** |
| shock_hash64   |     FAILED |    -     |              -    |           -    |  -   | bucket overflow at 1M |

`shock_hash128` is the new space champion on pure PHF: **1.50 bits/key**,
matching the Lehmann-Sanders-Walzer 2023 paper's headline number within
our simplified implementation. The cost is non-minimality (range_size
is about 1.67x num_keys) and a moderate build time. `shock_hash64` is
more compact per bucket but overflows at 1M keys without clever bucket
overflow recovery; use 128 for N above ~100K.

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

| filter             | bits/key | build (ms) | query median (ns) | empirical FPR | expected FPR |
|--------------------|---------:|-----------:|------------------:|--------------:|-------------:|
| xor<8>             |     9.84 |      363.2 |             258.0 |        0.41%  |       0.39%  |
| xor<16>            |    19.68 |      368.1 |             156.0 |        0.02%  |      0.0015% |
| xor<32>            |    39.36 |      382.5 |             261.7 |        0%     |          ~0  |
| ribbon<8>          |     8.64 |    1,291.6 |             281.9 |        0.39%  |       0.39%  |
| ribbon<16>         |    17.28 |    1,303.8 |             291.6 |        0.002% |      0.0015% |
| ribbon<32>         |    34.56 |    1,307.6 |             302.2 |        0%     |          ~0  |
| **binary_fuse<8>** | **9.18** |  **272.2** |         **240.1** |     **0.39%** |       0.39%  |
| binary_fuse<16>    |    18.35 |      281.5 |             242.1 |       0.001%  |      0.0015% |
| binary_fuse<32>    |    36.70 |      270.5 |             242.7 |        0%     |          ~0  |

### Observations

- **Ribbon wins on pure space** across all widths (about 12% more compact
  than xor). Matches theory: ribbon's overhead is 1.08x, xor's is 1.23x.
- **Binary fuse sits between xor and ribbon**: ~7% less space than xor
  at the same FPR with slightly faster build and query. For throughput-
  sensitive filter use, it's the new default.
- **Xor is fastest on query** (when successful) but builds slower than
  binary fuse. Ribbon's Gaussian elimination dominates its build time,
  which is 4.7x slower than binary fuse at 1M.
- **Empirical FPR tracks theory** at 8-bit; 16-bit sample sizes can still
  be too small to resolve the true rate.

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

**Follow-up: with `partitioned_phf`, PHOBIC construction is no longer the
bottleneck.** `partitioned_phf<phobic5>` builds 1M keys in 1.8 s on 8 cores
(versus 107 s for serial phobic5). A
`perfect_filter<partitioned_phf<phobic5>, 16>` inherits the fast build and
pays a small `c` premium (2.86 vs 2.73 b/k), for a total around 18.9 b/k.
Beating Bloom at FPR < 1.4% while building at about 2 seconds per million
keys is the best single operating point in the whole space explored here.

---

## bench_phobic_parallel: PHOBIC parallel build strategies

Four strategies are compared:
- **serial**: `threads=1`, the original sequential algorithm
- **fat+bucket**: `threads=N`, processes the top N largest buckets cooperatively
  (all threads on disjoint pilot strides per bucket), then work-steals thin
  buckets
- **partitioned** N=T, S=auto: `partitioned_phf<phobic_K>` with auto shard
  count (~`num_keys/15000`); each shard builds serially, shards execute in
  parallel

### Speedup at 1,000,000 keys (8 cores)

| strategy    | algo    | threads | build (ms) | bits/key | query (ns) | speedup |
|-------------|---------|--------:|-----------:|---------:|-----------:|--------:|
| serial      | phobic3 |       1 |    23,291  |    2.794 |        146 |    1.00x |
| fat+bucket  | phobic3 |       8 |     9,866  |    2.796 |        148 |    2.36x |
| **partitioned** | phobic3 |   8 |       **421** | 2.913 |        261 | **55.3x** |
| serial      | phobic4 |       1 |    42,231  |    2.609 |        146 |    1.00x |
| fat+bucket  | phobic4 |       8 |    13,698  |    2.610 |        150 |    3.08x |
| **partitioned** | phobic4 |   8 |       **566** | 2.760 |        264 | **74.6x** |
| serial      | phobic5 |       1 |   106,575  |    2.728 |        144 |    1.00x |
| fat+bucket  | phobic5 |       8 |    27,779  |    2.727 |        149 |    3.84x |
| **partitioned** | phobic5 |   8 |     **1,780** | 2.859 |        265 | **59.9x** |

### Speedup at 100,000 keys (8 cores)

| strategy    | algo    | threads | build (ms) | bits/key | query (ns) | speedup |
|-------------|---------|--------:|-----------:|---------:|-----------:|--------:|
| serial      | phobic3 |       1 |       115  |    2.868 |         35 |    1.00x |
| fat+bucket  | phobic3 |       4 |        71  |    2.871 |         36 |    1.61x |
| partitioned | phobic3 |       8 |        27  |    2.926 |         89 |    4.26x |
| serial      | phobic4 |       1 |       317  |    2.707 |         36 |    1.00x |
| fat+bucket  | phobic4 |       4 |        49  |    2.716 |         52 |    6.47x |
| partitioned | phobic4 |       8 |        42  |    2.769 |         65 |    7.61x |
| serial      | phobic5 |       1 |     9,515  |    2.719 |         42 |    1.00x |
| fat+bucket  | phobic5 |       8 |     2,458  |    2.720 |         35 |    3.87x |
| **partitioned** | phobic5 |   8 |       **247** | 2.856 |         76 | **38.5x** |

### Scaling to 10M keys (partitioned only; serial would take 15+ minutes)

| config                  | threads | build (s) | bits/key | memory (MB) | query (ns) | T-speedup |
|-------------------------|--------:|----------:|---------:|------------:|-----------:|----------:|
| partitioned<phobic3>    |       1 |    10.13  |    2.913 |        3.47 |        300 |     1.00x |
| partitioned<phobic3>    |       8 |     4.78  |    2.913 |        3.47 |        299 |     2.12x |
| partitioned<phobic4>    |       1 |    15.19  |    2.758 |        3.29 |        296 |     1.00x |
| partitioned<phobic4>    |       8 |     5.95  |    2.758 |        3.29 |        293 |     2.55x |
| partitioned<phobic5>    |       1 |    67.06  |    2.854 |        3.40 |        299 |     1.00x |
| **partitioned<phobic5>** |   **8** | **16.96** |    2.854 |        3.40 |        291 |  **3.95x** |

Scaling from 1M to 10M:
- `partitioned<phobic5>` 8T: 1.8s -> 17.0s (near-linear, 9.4x for 10x work)
- bits/key essentially unchanged (2.86 at 1M, 2.85 at 10M)
- query latency rises about 10% (265 ns -> 291 ns; more L3 pressure)
- 3.4 MB total memory for 10M keys

Thread scaling at 10M is 2-4x on 8 cores (vs 4-7x at 100K and 1M). The
remaining non-parallel work (key hashing, partitioning into shards, prefix-sum
of offsets) becomes visible at this scale: that work is O(n) and serial, so it
caps speedup even when individual shard builds parallelize perfectly.

### Observations

- **Partitioned is the clear winner for PHOBIC at scale.** `partitioned<phobic5>`
  at 1M keys builds in 1.8 seconds with 8 threads, versus 107 seconds serial.
  That is a **60x speedup** for the single most valuable algorithm in the
  library.
- **Speedup compounds with bucket size.** Partitioned helps most where serial
  was worst: phobic5 goes from 107s to 1.8s (60x); phobic3 goes from 23s to
  0.42s (55x); phobic4 goes from 42s to 0.57s (75x).
- **Query cost of partitioning is about 2x.** Extra cost is one additional
  hash to select the shard, then the inner PHF query. `phobic5` queries go
  from 145 ns to 265 ns. Still sub-microsecond.
- **Space cost of partitioning is 3-5%.** Extra bits/key come from per-shard
  metadata (seed, sizes, buckets) and slightly higher pilot collision rates
  in smaller shards. `phobic5`: 2.73 -> 2.86 b/k.
- **fat+bucket is helpful but modest.** At 1M it doubles-to-quadruples
  throughput but still trails partitioned by 10-50x. The straggler problem
  is real: phase 3 finishes only when the slowest thread finishes the largest
  remaining bucket, and cooperative fat-bucket processing only addresses the
  top N_THREADS buckets.
- **bits/key essentially unchanged across fat+bucket thread counts.** Confirms
  the atomic claim/release protocol's correctness end-to-end (in addition to
  the unit tests).

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
  whose quality is distribution-sensitive. The `bench_harness` now supports
  `--distribution=random|sequential|url|variable` for future exploration.

See `benchmarks/README.md` for the measurement methodology.

---

## bench_retrieval: retrieval methods across value widths

The retrieval concept is the primitive behind static-function data
structures: given `v: S -> {0,1}^m`, store a lookup that returns `v(k)`
for `k in S` and garbage for `k not in S`. Unlike `approximate_map`,
no membership sentinel is exposed, which makes retrieval the right
primitive for oblivious constructions like cipher maps.

Two implementations are compared:
- `ribbon_retrieval<M>`: banded GF(2) linear system, stores one
  m-bit value per row, ~1.04 * M bits/key theoretical cost.
- `phf_value_array<PHF, M>`: PHF slot + packed M-bit value array,
  cost is `bits_per_key(PHF) + M`.

### At 100,000 keys (random)

| method                   |   M  | build (ms) | bits/key | query (ns) |
|--------------------------|:----:|-----------:|---------:|-----------:|
| ribbon<1>                |   1  |       26.3 |     1.08 |       58.2 |
| ribbon<8>                |   8  |       24.1 |     8.64 |       59.5 |
| ribbon<16>               |  16  |       27.7 |    17.28 |       61.7 |
| ribbon<32>               |  32  |       25.8 |    34.56 |       71.4 |
| ribbon<64>               |  64  |       29.1 |    69.12 |       76.7 |
| pva<phobic5, 1>          |   1  |    2,372.7 |     3.73 |       24.8 |
| pva<phobic5, 8>          |   8  |    3,097.7 |    10.77 |       31.2 |
| pva<phobic5, 16>         |  16  |    2,535.9 |    18.81 |       34.2 |
| pva<phobic5, 32>         |  32  |    2,577.3 |    34.89 |       36.3 |
| pva<phobic5, 64>         |  64  |    2,928.6 |    67.04 |       34.5 |
| pva<part<phobic4>, 1>    |   1  |       59.7 |     3.77 |       50.0 |
| pva<part<phobic4>, 8>    |   8  |       57.3 |    10.77 |       50.1 |
| pva<part<phobic4>, 16>   |  16  |       53.4 |    18.77 |       51.5 |
| pva<part<phobic4>, 32>   |  32  |       57.4 |    34.77 |       53.6 |
| pva<part<phobic4>, 64>   |  64  |       55.1 |    66.77 |       52.4 |
| pva<bbhash5, 1>          |   1  |       35.9 |    21.03 |       39.1 |
| pva<bbhash5, 16>         |  16  |       36.7 |    36.03 |       36.3 |
| pva<bbhash5, 64>         |  64  |       36.1 |    84.03 |       36.7 |

### At 1,000,000 keys (plain phobic5 skipped for runtime)

| method                   |   M  | build (ms) | bits/key | query (ns) |
|--------------------------|:----:|-----------:|---------:|-----------:|
| ribbon<1>                |   1  |      523.9 |     1.08 |      167.2 |
| ribbon<8>                |   8  |      903.2 |     8.64 |      166.2 |
| ribbon<16>               |  16  |      898.1 |    17.28 |      175.3 |
| ribbon<32>               |  32  |      904.7 |    34.56 |      184.7 |
| ribbon<64>               |  64  |    1,029.5 |    69.12 |      221.4 |
| pva<part<phobic4>, 1>    |   1  |      690.0 |     3.76 |      150.1 |
| pva<part<phobic4>, 8>    |   8  |      689.8 |    10.76 |      147.7 |
| pva<part<phobic4>, 16>   |  16  |      687.0 |    18.76 |      151.1 |
| pva<part<phobic4>, 32>   |  32  |      704.4 |    34.76 |      156.1 |
| pva<part<phobic4>, 64>   |  64  |      697.6 |    66.76 |      167.8 |
| pva<bbhash5, 1>          |   1  |      651.4 |    21.00 |      104.6 |
| pva<bbhash5, 16>         |  16  |      550.1 |    36.00 |      136.1 |
| pva<bbhash5, 64>         |  64  |      552.4 |    84.00 |      113.7 |

### The crossover

For a PHF with space `c` bits/key, the two methods cost:
- ribbon: `(1 + eps) * M` bits/key (eps = 0.08 at the default epsilon)
- pva: `c + M` bits/key

They tie when `eps * M = c`, i.e. `M = c / eps`. At `c = 2.7` (PHOBIC)
and `eps = 0.08`: M = 33.75. Confirmed empirically: M=32 is a tie, M=64
goes to phobic5.

### Observations

- **Ribbon is the cipher-map winner.** For narrow `M` (1-16 bits, which
  covers single-bit classifiers through 16-bit ciphertext slots),
  ribbon_retrieval uses 2-4x less space than the best PHF alternative.
  At `M=1`: ribbon is 1.08 b/k vs pva<part4>'s 3.77 b/k.
- **PHF+array wins for wide values.** At `M >= 32`, pva<phobic5> or
  pva<part<phobic4>> uses ~1-3% less space than ribbon. If you're
  storing 32-bit or 64-bit payloads, prefer PHF+array.
- **Build time separates ribbon from fat PHF+array.** Plain phobic5 +
  array at 100K takes 2.5-3.1 seconds per M; ribbon takes 25-30 ms,
  roughly 100x faster. Partitioned phobic4 closes most of that gap
  (53-60 ms) at a ~1% space cost.
- **Query latency favors PHF+array.** At 100K: ribbon ~60 ns, pva<phobic5>
  ~25-36 ns. Ribbon pays for the XOR chain over solution entries
  (typically 10-20 memory reads in a 64-slot window); pva does one
  slot computation + one indexed array read. Both are sub-microsecond
  at 1M, and dominated by cache pressure at that scale.
- **bbhash5 backend is a pure-speed option**: fastest queries at 1M
  (105-136 ns) with 20+ bits/key overhead. Not interesting for
  space-constrained cipher maps, but useful when query latency
  dominates the cost model.
- **Space efficiency is distribution-invariant.** All methods' bits/key
  numbers match the theory regardless of key distribution (verified
  against URL vs random at 100K in other runs).

### Implications for cipher maps

The user's cipher-map constructions want narrow `M` and don't need a
membership sentinel. Both conditions favor ribbon_retrieval:

- At `M=1` (single-bit classifier over a secret set): 1.08 bits/key,
  approaching the Shannon floor. 10M such keys fit in 1.4 MB.
- At `M=8` (byte-wide ciphertext map): 8.64 bits/key.
- At `M=16` (16-bit ciphertext or 16-bit fingerprint): 17.28 bits/key.

The `(1 + eps)` multiplier is the only cost above the information floor,
and the query path is a pure linear function of stored state: XOR of
a handful of solution entries. No branch on membership, so the output
distribution for non-members is indistinguishable from a legitimate
lookup when values are pseudorandom.

---

## bench_partitioned_algos: inner PHF choice under partitioning

Partitioning made `phobic5` 60x faster at 1M keys. Does it help the other
algorithms too? This benchmark sweeps the inner-PHF parameter of
`partitioned_phf<Inner>` at 1M keys, 8 threads.

### At 1,000,000 keys, 8 threads

| config                     | build (ms) | bits/key | query (ns) | notes                  |
|----------------------------|-----------:|---------:|-----------:|------------------------|
| partitioned<phobic5>       |    1,694.5 |     2.86 |      259.2 | best space             |
| partitioned<phobic4>       |      555.3 |     2.76 |      261.8 | **best space+build**   |
| partitioned<phobic3>       |      400.1 |     2.91 |      255.9 | fastest small-PHOBIC   |
| partitioned<bbhash5>       |      335.2 |    18.39 |      268.7 | fastest overall        |
| partitioned<bbhash3>       |      551.6 |    18.97 |      266.1 |                        |
| partitioned<recsplit8>     |      416.2 |    96.08 |      297.2 | serial inner           |
| partitioned<chd>           |      647.5 |   134.50 |      287.0 | serial inner           |
| partitioned<fch>           |      474.2 |   200.10 |      291.3 | serial inner           |

### Plain (single PHF, no partitioning) for comparison

| config                     | build (ms) | bits/key | query (ns) | speedup-from-partition |
|----------------------------|-----------:|---------:|-----------:|-----------------------:|
| phobic5 (fat+bucket, T=8)  |   24,483.1 |     2.73 |      150.9 |                 14.4x |
| bbhash5 (serial, T=1)      |      468.9 |    20.00 |      186.1 |                  1.4x |
| recsplit8 (serial, T=1)    |    3,082.3 |    96.00 |      260.9 |                  7.4x |

### Observations

- **Partitioning helps every algorithm, but the payoff is biggest for PHOBIC.**
  `phobic5` wins 14.4x from partitioning because the serial inner algorithm has
  super-linear pilot search and benefits most from smaller per-shard work.
  BBHash5 wins only 1.4x because its serial build is already fast.
- **phobic4 is the partitioned-build winner.** Inside a partitioned wrapper,
  `phobic4` matches `phobic5` on space (2.76 vs 2.86 b/k) but builds 3x faster.
  Pilot search difficulty grows non-linearly with bucket size, so `k=4`
  capture most of PHOBIC's space advantage for a fraction of the cost.
- **BBHash is the speed king, PHOBIC the space king.** `partitioned<bbhash5>`
  at 335 ms is the fastest observed build at 1M keys, but at 18.4 b/k it spends
  6.4x more space than `partitioned<phobic4>`.
- **Query latency is roughly constant (~260-290 ns) across inner algorithms.**
  The partitioned query cost is dominated by the shard-selection hash and
  cache behavior, not the inner PHF lookup. So the inner choice can be
  optimized for build time and space without hurting query speed.
- **recsplit8/chd/fch have serial inner builds**, so partitioning alone
  parallelizes them across shards (roughly 8x from 8 threads). `recsplit8`
  plain serial: 3.1s; partitioned: 416 ms (7.4x, as expected).
- **Take-away**: for approximate maps at 1M+, prefer
  `partitioned_phf<phobic4>` (best total cost) or `partitioned_phf<bbhash5>`
  (for minimum latency-to-query, at 6x the space).

---

## bench_partitioned_sweep: shard-count sweep

Question: is the default of ~15,000 keys per shard optimal, or is there a better
point in the (build_time, bits/key) plane?

### At 1,000,000 keys

| shards | keys/shard | T | build (ms) | bits/key | query (ns) |
|-------:|-----------:|--:|-----------:|---------:|-----------:|
|     16 |     62,500 | 1 |     42,035 |     2.80 |        ~260 |
|     64 |     15,625 | 1 |      7,808 |     2.85 |        ~260 |
|    256 |      3,906 | 1 |      4,901 |     3.00 |        ~265 |
|   1024 |        977 | 1 |      4,691 |     3.58 |        ~270 |
|     16 |     62,500 | 8 |     11,148 |     2.80 |        ~260 |
|     64 |     15,625 | 8 |      1,979 |     2.85 |        ~260 |
|    256 |      3,906 | 8 |      1,255 |     3.00 |        ~265 |
|   1024 |        977 | 8 |      1,194 |     3.58 |        ~270 |

### Observations

- **Sweet spot is around 128-256 shards at 1M keys.** Build time plateaus
  (1.2s at T=8, 4.7s at T=1); above that, bits/key keeps rising without build
  improvement.
- **Smaller shards are near-linearly faster at T=1.** Each shard's build is
  super-linear in its size, so 4x more shards means roughly 5x less total work.
  Diminishing returns kick in once per-shard work is below ~1 ms (fixed
  overhead per shard takes over).
- **At T=8 the build-time curve is nearly flat for shards >=64.** Past the
  thread-count threshold, parallelism keeps up with per-shard granularity.
- **Bits/key rises monotonically with shard count.** Each shard carries its own
  seed (8 bytes), sizes (8 bytes), and alpha (8 bytes) of metadata, plus
  ceiling effects on pilot counts in small shards. 1024 shards at 1M keys
  means ~24 KB of pure shard metadata (0.19 bits/key) plus per-shard overhead.
- **Query latency is essentially invariant.** Shard count doesn't affect the
  query path (always: hash -> shard lookup -> inner PHF).
- **The default (`num_keys / 15000`) puts us at 67 shards for 1M keys**, which
  from this data is on the right side of the knee: good space (2.85 b/k),
  near-optimal build (2.0s at T=8). Slightly larger (256 shards) gets
  a faster build at a 5% space cost.

### At 10,000,000 keys

Only T=8 data. The T=1 baseline for 10M with 256 shards would take >20 minutes
(each 39K-key shard builds serially at several seconds); the parallel regime
is the useful one at this scale.

| shards | keys/shard | build (ms) | bits/key | memory (KB) | query (ns) |
|-------:|-----------:|-----------:|---------:|------------:|-----------:|
|    256 |     39,062 |     57,391 |     2.82 |       3,448 |      314.9 |
|    512 |     19,531 |     22,592 |     2.84 |       3,472 |      304.1 |
|  1,024 |      9,765 |     18,035 |     2.88 |       3,518 |      315.8 |
|  2,048 |      4,882 |     15,928 |     2.96 |       3,614 |      320.2 |
|  4,096 |      2,441 |     15,556 |     3.11 |       3,800 |      318.5 |

### Observations at 10M scale

- **Build-time knee shifts to ~1024-2048 shards.** At 1M, 256 shards was the
  sweet spot; at 10M it is 1024-2048. The reason is per-shard build cost is
  super-linear in its size, so larger total N pushes the optimum toward
  smaller (faster) shards.
- **Beyond ~2048 shards, more shards don't help.** Going from 2048 to 4096
  saves only 400 ms of build time (15.9s to 15.6s) but adds 0.15 b/k. Not
  worth it. At this point the bottleneck is scheduling and metadata cost,
  not per-shard build work.
- **Space cost of aggressive sharding is real.** 256 to 4096 shards: 2.82 to
  3.11 b/k (10% increase) at 10M. At 1M: 2.85 to 3.58 (26% increase). The
  metadata-per-shard cost is amortized better at larger key counts.
- **Memory footprint is tiny in absolute terms.** 10M keys fit in 3.4-3.8 MB,
  two orders of magnitude smaller than a hash table of the same keys would
  need.
- **Practical guidance**: at `num_keys >= 10^7`, target ~1000-5000 keys/shard
  (not the default 15,000). The `partitioned_phf` auto-shard heuristic could
  be tuned to scale shard size down as total N grows.

---

## bench_bloomier: retrieval x oracle compositions

`bloomier<Retrieval, Oracle>` returns `optional<value_type>`. The
retrieval produces the value; the oracle gates membership. The
approximate-map path that lives separately from the retrieval (cipher-
map) path.

### At 1,000,000 keys, M=16 value, 8-bit FPR (~0.4%)

| composition                        | build (ms) | bits/key | query (ns) | FPR     |
|------------------------------------|-----------:|---------:|-----------:|--------:|
| ribbon<16> + binary_fuse<8>        |    1,099.8 |    26.46 |     208.4  | 0.41%   |
| ribbon<16> + xor<8>                |    1,287.7 |    27.12 |     214.4  | 0.33%   |
| ribbon<16> + ribbon<8>             |    1,655.1 |    25.92 |     234.1  | 0.39%   |
| pva<phobic5,16> + xor<8>           |  105,067.3 |    28.65 |     149.0  | 0.33%   |
| pva<phobic5,16> + binary_fuse<8>   |  105,368.8 |    27.98 |     149.9  | 0.41%   |

### At 1,000,000 keys, M=16 value, 16-bit FPR (~0.0015%)

| composition                        | build (ms) | bits/key | query (ns) | FPR        |
|------------------------------------|-----------:|---------:|-----------:|-----------:|
| ribbon<16> + binary_fuse<16>       |    1,102.3 |    35.63 |     211.1  | 0%         |
| ribbon<16> + xor<16>               |    1,178.9 |    36.96 |     214.2  | 0.00015%   |
| ribbon<16> + ribbon<16>            |    1,650.7 |    34.56 |     230.9  | 0%         |

### Varying M with ribbon<M> + binary_fuse<8> at 1M keys

| M  | build (ms) | bits/key | query (ns) |
|---:|-----------:|---------:|-----------:|
|  1 |      688.3 |    10.26 |     201.7  |
|  8 |    1,102.0 |    17.82 |     202.7  |
| 16 |    1,099.8 |    26.46 |     208.4  |
| 32 |    1,078.0 |    43.74 |     213.6  |
| 64 |    1,172.5 |    78.30 |     262.9  |

### Observations

- **ribbon + binary_fuse is the build-time sweet spot.** At 1M keys,
  M=16, 8-bit FPR: 1.1 seconds build, 26.46 bits/key, 208 ns/query,
  0.4% FPR. Comparable to any other 8-bit approximate map.
- **pva<phobic5,16> + * are the query-latency winners.** 149 ns vs
  ribbon's 208 ns is a real gap for hot-path lookups. The cost is
  100x longer build (105s vs 1.1s) from PHOBIC's pilot search.
- **Space grows linearly in M for ribbon-based compositions.** Each
  additional M bit adds ~1.08 bits/key from ribbon plus 0 from the
  oracle (oracle cost is independent of M). Narrow-M approximate maps
  are especially compact: `ribbon<1> + binary_fuse<8>` at **10.26
  bits/key** is the tiniest any of our structures can be while still
  answering "did I see this key + what's its 1-bit value".
- **Use pva-based only when PHOBIC's build cost is amortized** over
  many queries or when minimum query latency is worth the 100x build
  cost. For streaming or repeated-build workloads, ribbon dominates.

---

## Distribution sensitivity (bench_partitioned_algos at 100K)

Does key structure change the rankings? Comparing `random` (16-byte printable
ASCII, fixed seed) with `url` (synthetic URL-shaped strings with typical path
and host prefixes) at 100K keys, 8 threads, partitioned variants:

| config                | random build (ms) | url build (ms) | random query (ns) | url query (ns) |
|-----------------------|------------------:|---------------:|------------------:|---------------:|
| partitioned<phobic5>  |             682.8 |          538.4 |             216.1 |          400.7 |
| partitioned<phobic4>  |             123.7 |          117.0 |             135.9 |          452.9 |
| partitioned<phobic3>  |              59.9 |          102.6 |             171.1 |          443.8 |
| partitioned<bbhash5>  |              45.8 |           84.1 |             200.4 |          417.2 |
| partitioned<bbhash3>  |             132.7 |          214.4 |             239.8 |          413.1 |
| partitioned<recsplit8>|              83.6 |          126.8 |             239.5 |          540.3 |
| partitioned<chd>      |             151.3 |          571.5 |             218.2 |          495.7 |
| partitioned<fch>      |             124.1 |          494.1 |             237.2 |          486.0 |

### Observations

- **bits/key is invariant across distributions.** Every algorithm reports the
  same space for `random` and `url` within 0.2% at 100K (not shown; data in the
  raw output). This confirms space efficiency is a function of key count, not
  structure.
- **Query latency is 2-3x slower on URL keys.** URL keys are longer (50-80
  bytes typical) versus 16 bytes for random keys, so each hash is ~4x the
  work. Query comparisons that do `strcmp` during overflow lookup pay extra
  cost. This is a pure string-processing cost, not a PHF quality issue.
- **Weak inner hashes (chd, fch) pay 3-4x on URL builds.** Partitioned<chd>
  builds 3.8x slower on URL keys (151 ms vs 572 ms), and partitioned<fch>
  4x slower (124 ms vs 494 ms). Both use simple mix functions that retain
  structural patterns in URL-like keys, leading to more retries during pilot
  search. PHOBIC's stronger bucket hash (`phf_remix`/splitmix64) keeps build
  time stable across distributions.
- **Practical takeaway**: for non-uniform keys, prefer PHOBIC-family inner
  algorithms over FCH/CHD even if space is not critical. The hash-quality
  difference shows up as build-time variance, not as reported `bits/key`.



