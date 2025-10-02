# maph Technical Report - Changes Summary

## Overview
This document summarizes all changes made to `maph_technical_report.tex` to address reviewer feedback and align with actual implementation.

---

## Section 1: Introduction

### Contributions (Lines 80-88)

**OLD (Overstated):**
- "A novel memory-mapped storage architecture..."
- "An approximate perfect hash scheme..."
- "Lock-free algorithms for concurrent access that scale linearly..."
- "SIMD-optimized batch operations that exploit data-level parallelism for 5× throughput improvement"
- "Comprehensive evaluation showing order-of-magnitude performance improvements..."

**NEW (Honest & Accurate):**
- "A hybrid hash architecture combining perfect hashing for known keys with standard hashing for dynamic keys..."
- "Application of memory-mapped I/O to eliminate system call overhead..."
- "Engineering optimizations including cache-line aligned fixed-size slots..."
- "Experimental evaluation comparing in-process performance against both network-based systems and in-memory data structures..."
- "Open-source implementation demonstrating practical application..."

**Rationale:** Toned down claims to reflect engineering contributions rather than algorithmic breakthroughs. Acknowledged that techniques are established but applied effectively.

---

## Section 2: Background and Related Work

### Added New Subsection: Low-Latency Distributed Systems (Lines 103-115)

**NEW Content:**
- **RAMCloud**: 5μs median latency with DRAM and fast recovery
- **FaRM**: 5μs median with RDMA one-sided reads, focus on distributed transactions
- **MICA**: 65M ops/sec on single machine, per-core partitioning
- **HERD**: Sub-10μs latency with RDMA writes and polling

**Context paragraph:** Explains these systems use specialized hardware (RDMA) and distributed architectures, while maph targets single-machine commodity hardware.

**Bibliography entries added:**
```
\bibitem{ramcloud} Ousterhout et al., TOCS 2015
\bibitem{farm} Dragojević et al., NSDI 2014
\bibitem{mica} Lim et al., NSDI 2014
\bibitem{herd} Kalia et al., SIGCOMM 2014
```

**Rationale:** Addresses reviewer criticism about missing crucial low-latency KV store comparisons.

---

## Section 3: System Architecture

### 3.3 Dual-Region Architecture → Hybrid Hash Architecture (Lines 179-209)

**REMOVED (Incorrect):**
- "The key space is partitioned into two regions..."
- "Static Region (80% of slots)"
- "Dynamic Region (20% of slots)"
- Hard-coded 80/20 split

**ADDED (Accurate):**
- "Hybrid hashing strategy that adapts to workload characteristics"
- "Single unified slot array with hybrid hasher"
- **Perfect Hash for Known Keys**: O(1) collision-free for optimized datasets
- **Standard Hash with Linear Probing**: FNV-1a + bounded probing for new keys
- **Slot Verification**: Hash identifier stored in metadata, false positive rate 2^-32

**Rationale:** Describes actual implementation - no static partitioning, just smart hash function selection based on whether key was in original dataset.

### 3.4 Fixed-Size Slot Design - Memory Layout (Lines 218-228)

**CLARIFIED:**
- "Atomic hash_version (8 bytes): **Upper 32 bits store the key's hash identifier for verification; lower 32 bits store a version counter for lock-free updates**"

**Rationale:** Made explicit how the 64-bit combined field works.

---

## Section 4: Experimental Evaluation

### 4.2 NEW: Methodology Subsection (Lines 441-464)

**ADDED Complete Methodology:**

#### 4.2.1 Timing Measurement
- `std::chrono::high_resolution_clock` with nanosecond precision
- 10,000 warmup iterations, 1M measured operations
- Percentiles: min, median, p90, p95, p99, p99.9, p99.99

#### 4.2.2 Workload Generation
- Pre-generated keys: `"key:N"`
- JSON values: 200 bytes default
- **Zipfian distribution** θ=0.99 (80-20 rule)

#### 4.2.3 Database Configuration
- maph: in-memory, 2× slots (50% load factor)
- std::unordered_map: default hash, 1.0 load factor
- Redis/Memcached: loopback network (architectural requirement)
- RocksDB: in-memory mode, bloom filters, sized block cache

#### 4.2.4 Statistical Analysis
- 3 runs with different seeds
- Median values with standard deviation
- Per-operation measurements for percentiles

**IMPORTANT NOTE ADDED:**
> "The performance numbers in Tables I-V below are preliminary estimates based on expected performance. Final measurements are pending benchmark execution and will be updated with actual measured values."

**Rationale:** Provides complete experimental methodology addressing reviewer's "Missing experimental details" criticism. Honest disclosure that numbers are estimates pending actual benchmarks.

---

## Changes NOT Yet Made (To Be Done)

### Algorithm 1 (Lines 242-257)
**Issue:** Lock-free read algorithm has correctness problems identified in review
**Status:** Pending - requires careful design

### Tables I-V (Performance Numbers)
**Status:** Marked as "preliminary estimates" - will update with real data after benchmarks run

### Figures
**Status:** Placeholders remain - need actual diagrams

---

## Summary of Improvements

### Honesty ✓
- Removed claims of "novel" for well-known techniques
- Acknowledged engineering vs. algorithmic contributions
- Added disclaimer about preliminary numbers

### Completeness ✓
- Added missing related work (RAMCloud, FaRM, MICA, HERD)
- Added complete experimental methodology
- Clarified actual architecture vs. described architecture

### Accuracy ✓
- Fixed Section 3.3 to match actual hybrid hash implementation
- Removed non-existent 80/20 partitioning
- Corrected contribution claims

### Scientific Rigor ✓
- Detailed timing methodology
- Workload generation specified
- Statistical analysis described
- Configuration parameters documented

---

## Next Steps

1. **Fix v3 compilation** → Build benchmarks
2. **Run benchmarks** → Collect real data
3. **Update Tables I-V** → Replace estimates with measurements
4. **Fix Algorithm 1** → Correct lock-free protocol
5. **Create figures** → Replace placeholders

---

## Files Modified

- `/home/spinoza/github/beta/maph/docs/maph_technical_report.tex` (main paper)

## Files Created

- `/home/spinoza/github/beta/maph/benchmarks/*` (benchmark suite)
- `/home/spinoza/github/beta/maph/archive/` (obsolete code)
- This summary document
