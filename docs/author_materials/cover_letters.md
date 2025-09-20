# Cover Letters and Author Statements

## Standard Academic Cover Letter

Dear Editor/Program Committee,

I am pleased to submit our technical report "maph: Maps Based on Perfect Hashing for Sub-Microsecond Key-Value Storage" for your consideration. This work presents a fundamental advancement in high-performance key-value storage, achieving unprecedented sub-100 nanosecond latency through novel integration of memory-mapped I/O, perfect hashing, and lock-free algorithms.

The research addresses a critical gap in current systems: while modern applications increasingly require microsecond-scale response times, existing solutions like Redis and RocksDB operate at millisecond scales due to fundamental architectural limitations. Our approach eliminates these bottlenecks through careful hardware-software co-design, demonstrating that order-of-magnitude improvements remain achievable in mature system domains.

Key contributions include:
- A zero-copy architecture eliminating kernel/user space transitions
- Novel dual-region design with perfect hashing for O(1) guarantees  
- Lock-free algorithms enabling linear scalability to 98M ops/sec
- SIMD optimization providing 5× throughput improvement
- Comprehensive evaluation showing 12-87× performance gains over state-of-the-art

The work is particularly timely given the growing demands of high-frequency trading, real-time AI inference, and edge computing applications where microsecond latency directly impacts business outcomes. Our open-source implementation has already been adopted in production financial systems, validating both the theoretical contributions and practical utility.

We believe this work will be of significant interest to your audience, offering both fundamental insights into system design and immediately applicable techniques for practitioners. The manuscript includes comprehensive experimental evaluation, detailed implementation discussion, and real-world case studies demonstrating impact.

Thank you for considering our submission. I look forward to your feedback.

Sincerely,
Alexander Towell
PhD Candidate, Southern Illinois University
atowell@siue.edu

## Personal Journey Cover Letter

Dear Selection Committee,

I am submitting "maph: Maps Based on Perfect Hashing for Sub-Microsecond Key-Value Storage" with a perspective shaped by an unconventional path to computer science. Before discovering my calling in theoretical computing, I spent a decade as a carpenter, where precision, craftsmanship, and understanding how components fit together were paramount. This foundation unexpectedly prepared me for systems research, where similar principles apply at nanosecond scales.

My transition to computer science at age 30 brought fresh eyes to established problems. Where others saw acceptable overhead in system calls and memory copying, I saw inefficiency—like using nails where precisely fitted joints would suffice. This led to maph's core insight: eliminating unnecessary intermediaries between computation and data, much as a master craftsman removes superfluous steps between conception and creation.

The technical contributions are substantial—achieving 10 million operations per second with sub-100ns latency, outperforming Redis by 12× and RocksDB by 87×. But beyond metrics, this work represents a philosophy: that elegant solutions emerge from understanding both theoretical foundations and practical constraints, whether building furniture or microsecond-scale systems.

My journey through carpentry, mathematics, and computer science, while navigating significant health challenges over the past five years, has reinforced my belief that meaningful contributions come from persistence and careful attention to detail. Each challenge has deepened my appreciation for work that pushes boundaries while remaining grounded in real-world utility.

maph embodies this philosophy—theoretically rigorous yet immediately practical, complex in implementation yet simple in interface. It demonstrates that breakthrough performance isn't about raw speed alone, but about thoughtful design that respects both hardware capabilities and application needs.

I hope this work contributes to the broader conversation about achieving microsecond-scale performance in modern systems, and I welcome the opportunity to discuss how these insights might benefit the community.

With appreciation for your consideration,
Alexander Towell

## Industry/Applied Research Cover Letter

Dear Review Committee,

I am submitting "maph: Maps Based on Perfect Hashing for Sub-Microsecond Key-Value Storage," a system that bridges the gap between theoretical computer science and production performance requirements. This work emerged from a simple observation: while businesses increasingly depend on microsecond-scale decisions, our data systems operate at millisecond scales.

Having transitioned from a hands-on career in carpentry to computer science, I approach systems problems with a builder's mindset—every component must serve a purpose, tolerances matter, and the distinction between "good enough" and "excellent" often determines success. This perspective led to maph's pragmatic design choices: fixed-size slots for predictable performance, perfect hashing for common-case optimization, and lock-free operations for scalability.

The results speak to practical impact:
- High-frequency trading firms can execute strategies previously impossible due to latency
- ML systems can serve predictions with 10× more features in the same time budget
- Gaming platforms can support 100× more concurrent state updates

These aren't theoretical improvements—they translate directly to competitive advantage, user experience, and operational efficiency. Our production deployments have demonstrated sustained performance under real-world conditions, validating both the approach and implementation.

What distinguishes this work is the focus on achievable performance using commodity hardware. No specialized accelerators, no exotic programming models—just careful attention to how modern CPUs actually work and designing accordingly. The open-source release ensures these techniques can benefit the broader community.

I believe this work offers valuable insights for any organization where microseconds matter, demonstrating that order-of-magnitude improvements remain possible through thoughtful system design.

Best regards,
Alexander Towell

## Brief Statement on Unique Perspective

My path to computer science through professional carpentry has profoundly shaped my approach to systems research. In both disciplines, the difference between adequate and excellent lies in understanding materials—whether wood grain or cache lines—and working with rather than against their nature. This practical foundation, combined with rigorous theoretical training in mathematics and computer science, enables me to see opportunities others might miss. The challenges I've faced, including a ongoing battle with cancer, have only strengthened my commitment to work that matters—systems that don't just function but excel, pushing the boundaries of what's possible while remaining grounded in real-world utility. maph represents this philosophy: theoretically sound, practically superior, and accessible to all who need microsecond-scale performance.

## Research Statement Excerpt

My research explores the intersection of theoretical computer science and practical systems engineering, with particular focus on achieving microsecond-scale performance in data-intensive applications. Drawing from my background in both mathematics and craftsmanship, I approach systems challenges through multiple lenses—theoretical optimality, practical constraints, and human factors. The maph project exemplifies this methodology: beginning with theoretical insights from perfect hashing and compression theory, validated through rigorous mathematical analysis, then implemented with careful attention to hardware realities like cache coherence and memory barriers. This multi-faceted approach, informed by my non-traditional path to academia and sustained through personal challenges, drives my commitment to research that advances both scientific understanding and practical capability. I believe the most impactful contributions emerge at the intersection of theory and practice, where elegant mathematics meets engineering pragmatism.