# maph benchmark suite

Four benchmarks, each aligned with one of the library's concepts:

| Benchmark | Concept | What it compares |
|-----------|---------|------------------|
| `bench_phf` | `perfect_hash_function` | All PHF implementations at multiple key scales |
| `bench_phf_sweep` | `perfect_hash_function` | Parameter sweeps within one algorithm family |
| `bench_filter` | `membership_oracle` | Standalone approximate-membership structures |
| `bench_approximate_map` | `approximate_map` | PHF + fingerprint compositions |

All benchmarks share `bench_harness.hpp` and emit TSV to stdout, progress to stderr.

## Build

```bash
cmake -DBUILD_BENCHMARKS=ON .. && make -j bench_phf bench_phf_sweep bench_filter bench_approximate_map
```

## Usage

```bash
./bench_phf                         # default: 10K 100K 1M
./bench_phf 1000 10000              # custom key counts
./bench_filter 10000 100000         # filter benchmark
./bench_approximate_map 10000       # composition benchmark
./bench_phf_sweep 50000             # parameter sweep at 50K keys
```

## Measurement methodology

**Query latency** is measured by timing *batches* of queries, not individual
ones. `chrono::now()` adds ~20-30 ns of overhead per call, which is comparable to a
fast PHF query. Per-query timing therefore distorts the measurement. Instead:

- A single "outer" batch of N=1,000,000 queries gives the throughput number
  (MQPS). One pair of clock reads amortizes to sub-ns per query.
- M=1000 "inner" sub-batches of B=1000 queries each produce M samples of
  "average ns per query for this sub-batch." The median and p99 of those
  samples approximate the query-latency distribution. Not identical to
  per-query p99 (we cannot measure that at sub-ns resolution), but stable
  enough for algorithm comparison.

**Build peak RSS** uses `/proc/self/clear_refs` + `/proc/self/status:VmHWM`.
Call `reset_peak_rss()` just before a build, `get_peak_rss_kb()` just after.
Works on Linux 2.6.22+; returns 0 silently on other platforms.

**Value sink** prevents dead-code elimination. A `volatile uint64_t sink`
that XORs in each query result: compiler can't prove the result is unused,
but the XOR is single-cycle and doesn't perturb timing.

## Output columns

```
algorithm  keys  range  build_ms  build_peak_kb  bits_per_key  mem_bytes  ser_bytes
           query_med_ns  query_p99_ns  throughput_mqps  fp_rate  ok
```

- `range` = `range_size()`. When larger than `keys`, the builder used a
  non-minimal configuration (e.g., PHOBIC bumped alpha to find a valid pilot).
- `build_peak_kb` = kilobytes of resident memory the process grew to during
  build (not including steady-state stuff before the build started).
- `bits_per_key` = space in the final structure only (pure PHF or pure
  filter; not build-time scratch). For composed maps, includes both the
  PHF and the fingerprint array.
- `ser_bytes` may differ from `mem_bytes` when the on-disk format differs
  from in-memory (e.g., overflow pilot metadata).
- `fp_rate` = empirical false-positive rate, `0` for pure PHFs (no FP
  semantics), measured against 100K unknown keys for filters/maps.

## Reproducibility

All benchmarks use fixed seeds:
- Key generation: seed 42
- Query index sequence: seed 12345
- Unknown-key generation (for FPR): seed 99999

Results are deterministic for a given binary and key count.

## What's not measured (yet)

- **Cache-cold queries**. Current measurements are after warmup, which
  matches typical "lookup server" workloads. A "one-shot cold cache" number
  would also be useful but requires more complex setup.
- **Cost of construction memory** beyond peak RSS. Allocator churn isn't
  broken out.
- **Real-world key distributions**. Currently only random 16-byte binary
  keys. URL-shaped, natural-language, or integer-as-string keys would likely
  shift the relative rankings.
