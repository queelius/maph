# maph Technical Report

## Overview

This directory contains the technical report for maph (Memory-Mapped Approximate Perfect Hash), a high-performance key-value database achieving sub-microsecond latency through innovative system design.

## Files

- **maph_technical_report.pdf** - The compiled technical report (7 pages, 188KB)
- **maph_technical_report.tex** - LaTeX source code

## Key Findings

The report documents how maph achieves:
- **10M ops/sec** single-threaded throughput
- **<100ns** GET latency (guaranteed O(1) with perfect hashing)
- **98M ops/sec** with 16 threads through lock-free operations
- **12× faster** than Redis, **87× faster** than RocksDB

## System Innovations

1. **Zero-copy architecture via mmap** - Eliminates kernel/user space transitions
2. **Perfect hash optimization** - Guarantees O(1) worst-case lookups
3. **Lock-free atomic operations** - Enables linear scalability
4. **SIMD batch processing** - 5× throughput improvement with AVX2
5. **Fixed 512-byte slots** - Predictable latency and cache-friendly

## Compilation

To recompile the PDF from source:

```bash
pdflatex maph_technical_report.tex
bibtex maph_technical_report
pdflatex maph_technical_report.tex
pdflatex maph_technical_report.tex
```

## Citation

If you use this work in your research, please cite:

```bibtex
@techreport{maph2024,
  title = {maph: Achieving Sub-Microsecond Key-Value Storage Through Memory-Mapped Perfect Hashing},
  author = {queelius},
  year = {2024},
  institution = {Independent Research},
  url = {https://github.com/queelius/rd_ph_filter}
}
```

## Abstract

maph is a high-performance key-value store that achieves sub-microsecond latency through a novel combination of memory-mapped storage, perfect hash functions, and lock-free operations. By leveraging modern hardware capabilities including SIMD instructions and cache-line optimization, maph delivers 10M operations per second on a single thread with median latencies below 100 nanoseconds.

## Contact

- GitHub: [https://github.com/queelius/rd_ph_filter](https://github.com/queelius/rd_ph_filter)
- Documentation: [https://queelius.github.io/rd_ph_filter/](https://queelius.github.io/rd_ph_filter/)