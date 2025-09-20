# Submission Checklist for Academic Venues

## Author Bio Selection Guide

### arXiv Submission
- **Bio**: Use 50-word short bio
- **Abstract**: Technical abstract (250 words)
- **Additional**: Include GitHub link and acknowledgments

### Top-Tier Systems Conferences (SOSP, OSDI, NSDI, EuroSys, ATC)
- **Bio**: Use 100-word medium bio
- **Abstract**: Technical abstract (250 words)
- **Cover Letter**: Standard academic or personal journey version
- **Emphasis**: Hardware-software co-design, performance evaluation, systems contributions

### Database Conferences (SIGMOD, VLDB, ICDE)
- **Bio**: Use 100-word medium bio highlighting database expertise
- **Abstract**: Technical abstract with database-specific terminology
- **Cover Letter**: Standard academic
- **Emphasis**: Perfect hashing, storage optimization, query performance

### Performance/HPC Venues (SC, HPDC, ICS)
- **Bio**: Use medium bio emphasizing HPC background
- **Abstract**: Technical abstract highlighting performance metrics
- **Cover Letter**: Standard academic with performance focus
- **Emphasis**: SIMD optimization, scalability, microsecond-scale achievements

### Industry/Applied Conferences (USENIX ATC, HotStorage)
- **Bio**: Use medium bio with practical emphasis
- **Abstract**: Executive summary (150 words) or condensed (100 words)
- **Cover Letter**: Industry/applied research version
- **Emphasis**: Production deployments, real-world impact, open source

### Journal Submissions (IEEE Transactions, ACM Transactions)
- **Bio**: Extended bio (200 words)
- **Abstract**: Technical abstract (250 words)
- **Cover Letter**: Personal journey or standard academic
- **Emphasis**: Comprehensive evaluation, theoretical foundations, broad impact

### Magazine/Newsletter Articles (CACM, IEEE Computer)
- **Bio**: Personal bio (150 words)
- **Abstract**: Layperson abstract (150 words)
- **Cover Letter**: Personal journey version
- **Emphasis**: Accessibility, broad impact, practical applications

## Key Submission Points to Emphasize

### For Systems Venues
1. Zero-copy architecture via mmap
2. Lock-free scalability to 98M ops/sec
3. Sub-100ns latency achievement
4. Comparison with Redis, RocksDB, Memcached

### For Database Venues
1. Perfect hash functions for O(1) guarantees
2. Dual-region architecture (80/20 split)
3. Space efficiency and memory layout
4. YCSB benchmark results

### For HPC/Performance Venues
1. SIMD acceleration (5× improvement)
2. Cache-line optimization
3. Linear scalability with thread count
4. Hardware-software co-design principles

### For Industry/Applied Venues
1. Production deployments in HFT and ML
2. Open-source availability
3. Practical performance gains (12-87×)
4. Real-world case studies

## Biographical Elements to Include/Exclude

### Always Include
- PhD student status at SIUE/SIUC
- MS degrees in CS and Math/Statistics
- Research interests in HPC, databases, ML
- Email: atowell@siue.edu
- GitHub: @queelius (for technical venues)

### Selectively Include
- **Career transition** (carpentry → CS): Include for venues valuing diversity, personal journey, or when explaining unique perspective
- **Health challenges**: Only mention when specifically relevant to narrative about persistence/determination
- **Running/athletics**: Include for personal/blog bios or when discussing work-life balance
- **Theoretical interests** (Hutter, compression): Include for theory-oriented venues or when discussing foundations

### Venue-Specific Adaptations
- **Financial/HFT venues**: Emphasize microsecond latency, production deployments
- **ML/AI venues**: Highlight feature store applications, batch operations
- **Open source venues**: Emphasize community contribution, accessibility
- **European venues**: May appreciate theoretical foundations more
- **US industry venues**: Focus on practical impact, benchmarks

## Tone Guidelines

### Academic Venues
- Formal but accessible
- Technical precision
- Objective performance claims
- Comprehensive citations

### Industry Venues  
- Direct and results-focused
- Business impact emphasis
- Practical benefits
- Minimal theoretical discussion

### Personal/Blog
- Conversational and engaging
- Personal journey elements
- Metaphors and analogies
- Broader life context

## Response to Common Questions

### "Why perfect hashing?"
Perfect hashing provides mathematical guarantee of O(1) lookup time, eliminating the uncertainty of traditional hash tables. For latency-critical applications, predictability is as important as average-case performance.

### "Why not use existing solutions?"
Existing solutions operate at microsecond to millisecond scales due to fundamental architectural choices (kernel crossings, memory copying, lock-based synchronization). maph demonstrates that order-of-magnitude improvements are possible through co-design.

### "What about write performance?"
While optimized for reads, maph maintains competitive write performance through lock-free updates and atomic versioning. The 80/20 static/dynamic split reflects real-world access patterns where most keys stabilize after initial insertion.

### "How does this relate to your background?"
The transition from carpentry to computer science brings a unique perspective on building robust systems. Both require understanding materials (physical or computational), respecting constraints, and crafting elegant solutions from basic components.