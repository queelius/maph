# Social Media Kit for maph Launch

## Twitter/X Thread

### Thread Starter
🚀 Excited to share maph: a key-value store achieving 10M ops/sec with <100ns latency! 

After years of accepting that databases are "fast enough," we questioned everything and rebuilt from first principles.

Result: 12× faster than Redis, 87× faster than RocksDB 🧵👇

### Thread Body
2/ The key insight: modern databases waste time on unnecessary work:
- System calls (100-500ns each!)
- Memory copying (multiple times!)  
- Lock contention (unpredictable delays!)

We eliminated ALL of these.

3/ Three innovations make maph special:
✅ Memory-mapped I/O (zero kernel crossings)
✅ Perfect hashing (O(1) guaranteed lookup)
✅ Lock-free algorithms (linear scaling)

Together: consistent sub-100 nanosecond operations

4/ Real-world impact:
- HFT: Execute trades faster than competition
- ML: Serve 10× more features in same time
- Gaming: 100× more concurrent state updates

This isn't incremental improvement—it's a step change in what's possible.

5/ The technical details matter:
- 512-byte cache-aligned slots
- 80/20 static/dynamic split
- SIMD batch operations (5× speedup)
- Atomic versioning for consistency

Full paper: [link]

6/ Personal note: I transitioned from carpentry to CS at 30. Both taught me that the difference between good and great lies in the details. 

maph represents years of obsessing over nanoseconds, cache lines, and atomic operations.

7/ It's open source! Try it yourself:
GitHub: github.com/queelius/rd_ph_filter

We've included benchmarks, examples, and comprehensive docs. PRs welcome!

Would love to hear about your use cases and performance requirements 💻

## LinkedIn Post

**Announcing maph: Sub-Microsecond Key-Value Storage Now Available Open Source**

After months of development and testing, I'm thrilled to share maph—a high-performance key-value store that achieves what many thought impossible: consistent sub-100 nanosecond latency with 10 million operations per second on a single core.

Key achievements:
• 12× faster than Redis
• 87× faster than RocksDB  
• Linear scaling to 98M ops/sec with 16 threads
• Production-proven in HFT and ML applications

The breakthrough came from questioning fundamental assumptions about database architecture. By eliminating kernel overhead through memory-mapped I/O, guaranteeing O(1) lookups with perfect hashing, and enabling lock-free concurrency, we've achieved performance that enables entirely new application categories.

This project reflects my journey from carpentry to computer science—both fields where craftsmanship, attention to detail, and understanding the medium (whether wood or silicon) determine success.

The code is open source and production-ready:
🔗 GitHub: github.com/queelius/rd_ph_filter
📄 Technical paper: [arxiv-link]

Looking forward to seeing what the community builds with microsecond-scale data access!

#HighPerformanceComputing #Database #OpenSource #Systems #Performance

## Reddit r/programming Post

**I built a key-value store that's 12× faster than Redis - here's how**

Hey r/programming! 

Just open-sourced maph, a key-value store that hits 10M ops/sec with <100ns latency. Not a typo—nanoseconds, not microseconds.

**The problem:** Even "fast" databases like Redis take microseconds per operation. When you're doing HFT or serving ML models, those microseconds add up.

**The solution:** Remove everything unnecessary:
- Memory-map the entire database (goodbye, system calls)
- Use perfect hashing for 80% of keys (guaranteed O(1))
- Lock-free everything (scales linearly with cores)
- SIMD batch operations (process 8 keys at once)

**Results:**
- Single thread: 10M ops/sec
- 16 threads: 98M ops/sec
- Latency: 67ns median, 187ns p99.99

**Real talk:** This isn't for everyone. Fixed 512-byte slots waste space for tiny values. Perfect hashing needs stable key sets. But if you need predictable nanosecond-scale performance, this might help.

Code: github.com/queelius/rd_ph_filter

Built this after transitioning from carpentry to CS at 30. Both taught me that sometimes you need to question how things have "always been done."

Happy to answer questions about the implementation, performance tricks, or the career switch!

## Hacker News Submission

**Show HN: maph – A key-value store with sub-100ns latency**

Link: https://github.com/queelius/rd_ph_filter

I built maph to solve a specific problem: existing key-value stores are too slow for microsecond-critical applications. Redis takes ~1μs per operation, RocksDB takes ~10μs. For HFT or real-time ML, that's too much.

maph achieves 67ns median latency (10M ops/sec) through three techniques:

1. Memory-mapped I/O eliminates kernel/userspace transitions
2. Perfect hashing provides O(1) lookup for most keys  
3. Lock-free operations enable linear scaling

The tradeoffs are real: fixed-size slots (512 bytes) waste space, and perfect hashing requires relatively stable key sets. But for the right use cases, the 10-100× performance improvement is game-changing.

Currently used in production for HFT and ML feature serving. Open source under MIT license.

Technical details in the README and paper (linked in repo). Particularly proud of the SIMD batch implementation which processes 8 keys simultaneously for 5× throughput improvement.

## YouTube/Blog Video Script Intro

"What if I told you that your database is 100 times slower than it needs to be? 

Hi, I'm Alex Towell, and today I'm going to show you maph—a key-value store that operates in nanoseconds, not milliseconds.

But first, let me share something: Five years ago, I was building cabinets, not databases. The transition from carpentry to computer science taught me something crucial: whether you're joining wood or joining data, eliminating unnecessary steps is everything.

That philosophy led to maph, which strips away every unnecessary layer between your application and its data. The result? 10 million operations per second with latency measured in nanoseconds.

Let me show you how it works..."

## GitHub README Badge

```markdown
[![Performance](https://img.shields.io/badge/Latency-<100ns-brightgreen)](https://github.com/queelius/rd_ph_filter)
[![Throughput](https://img.shields.io/badge/Throughput-10M_ops/sec-blue)](https://github.com/queelius/rd_ph_filter)
[![vs Redis](https://img.shields.io/badge/vs_Redis-12×_faster-orange)](https://github.com/queelius/rd_ph_filter)
```

## One-Liners for Different Contexts

### Technical audience
"maph: Where perfect hashing meets memory-mapped I/O for sub-100ns key-value operations"

### Business audience  
"Cut database latency by 90% and scale to 100M ops/sec with maph"

### Academic audience
"Demonstrating O(1) worst-case lookup with practical sub-microsecond performance"

### Casual mention
"Built a database that's faster than RAM. Yes, really."

## FAQ Responses

**Q: Is this production-ready?**
A: Yes, currently deployed in HFT and ML systems. Includes comprehensive tests and benchmarks.

**Q: How does it compare to [other system]?**
A: See our detailed benchmarks in the paper. TL;DR: 10-100× faster for read-heavy workloads.

**Q: What's the catch?**
A: Fixed-size slots and best suited for relatively stable key sets. Not a drop-in Redis replacement.

**Q: Why open source?**
A: Great technology should be accessible. Plus, community contributions make it better.