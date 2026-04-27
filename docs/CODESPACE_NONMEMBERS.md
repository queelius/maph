# Codespace controls non-member output: a measured claim

## The question

You have a partial function `f: S -> V` over a key set `S`, encoded into an
approximate-map data structure. For a key `k` not in `S`, the structure
returns *some* value: the lookup is not gated by membership (no oracle).
What value does it return, and what controls that?

This document reports the empirical answer for `encoded_retrieval<ribbon, codec>`
in maph, and discusses the design implications for cipher-map style
constructions where the absence of a membership signal is a feature.

## Three candidate answers, only one of which is right

### Candidate 1: "It returns the stored value of some S-member."

This is the right answer for `phf_value_array` over a *minimal* PHF: every
slot is used by some `k in S`, and a non-member's `phf.slot_for(k)` lands
on one of those used slots, returning that S-member's stored value. The
non-member distribution then equals the *empirical* distribution of `f`
on `S`.

For `ribbon_retrieval`, this answer is wrong. Ribbon does not "store at
slot i"; it stores XOR-decomposed equation coefficients. A non-member
query does not pick up any single S-member's value. See below.

### Candidate 2: "It depends on the storage frequency of values."

The intuitive guess: if 90% of `S` has `f(x) = A`, then 90% of the stored
patterns encode A, so non-member queries should return A about 90% of
the time. This is what most users assume on first reading.

We tested this directly: build two `encoded_retrieval<ribbon<4>, prefix_codec<V, 4>>`
structures with the **same codec** but radically different storage
distributions (99/0.5/0.5 vs 50/25/25 for A/B/C). The non-member
distributions are essentially identical and depend on the codec, not
the storage. The storage frequency does *not* propagate to non-members.

### Candidate 3: "It depends on the codec's codespace allocation."

The codec assigns each value `v` a class of M-bit patterns of size
`2^(M - l_v)` for prefix-free codeword length `l_v`. The fraction of
the codespace allocated to `v` is `2^(-l_v)`.

For ribbon retrieval, non-member lookups produce approximately uniform
random patterns in `GF(2)^M`. Decoding via the codec maps each pattern
to a value in `V`. So the probability that a non-member returns `v` is
the fraction of the codespace assigned to `v`.

This is the right answer for ribbon-based encoded retrieval, and is
what `tests/v3/test_prefix_codec.cpp` verifies.

## The verified empirical claim

> For `encoded_retrieval<ribbon_retrieval<M>, codec>` with any reasonable
> codec, the non-member output distribution is:
>
>     P(lookup(k_unknown) = v)  ~=  |class(v)| / 2^M
>
> where `class(v)` is the set of M-bit patterns that decode to `v` under
> the codec.

Predicted vs observed for prefix lengths {A: 1, B: 2, C: 2} on M=4:

| value | codespace share | observed (n=30000) | absolute error |
|------|----------------:|-------------------:|---------------:|
|  A   | 0.500 | 0.496 | 0.004 |
|  B   | 0.250 | 0.252 | 0.002 |
|  C   | 0.250 | 0.252 | 0.002 |

Test **non-member distribution matches codec codespace** in
`tests/v3/test_prefix_codec.cpp` enforces |error| < 0.03 with 30K samples.
At sample size N=30000 the binomial standard error is sqrt(p(1-p)/N) ~= 0.003,
so 3% is generous.

The contrastive test **non-member distribution: same codec, different
storage frequencies** verifies that two structures built from the same
codec but different storage distributions produce non-member distributions
within 5% of each other (and within 3% of codespace shares). This
isolates the codec as the causal lever.

## Why ribbon's non-member output is approximately uniform

Ribbon stores a `solution_` vector of `value_type` entries, computed by
forward Gaussian elimination + back-substitution of a sparse linear
system over `GF(2)^M`. For a key `k`, lookup is XOR of three positions
within a 64-slot band:

    lookup(k) = solution[h_1(k)] XOR solution[h_2(k)] XOR solution[h_3(k)]

For `k in S`, the build phase chose `solution_` so this XOR equals the
stored value of `k`. For `k not in S`, the same XOR is computed, but
the build had no constraint forcing any particular result.

Because the linear system is heavily overdetermined relative to the
band width and the ribbon paper's analysis applies, the back-substituted
`solution_` entries become approximately uniform random in `GF(2)^M`.
XOR of three approximately-uniform random values is itself approximately
uniform random. Therefore the non-member output is approximately a
uniform M-bit pattern, independent of the stored-value distribution.

This is *not* the case for trivial degenerate inputs (e.g., all stored
values are pattern 0, in which case the simplest solution is all zeros
and the non-member output is always 0). For a real workload with at
least a few distinct stored patterns, the uniformity holds.

## Design implication: cipher-map non-member control

Cipher-map use cases want non-member queries to return some specific
"default" or distribution-of-defaults rather than leaking membership.
The recipe is:

1. **Pick the target non-member distribution** `D` over `V`.
2. **Build a codec** whose codespace allocation matches `D`. For Huffman
   from frequencies, this falls out of `prefix_codec::from_frequencies`.
   For an explicit allocation, use `prefix_codec` with hand-picked
   `(value, length)` pairs subject to Kraft.
3. **Build the retrieval** with `encoded_retrieval<ribbon_retrieval<M>, codec>`
   on the actual key set and value function. The retrieval enforces
   `lookup(k) = f(k)` for `k in S`; non-members get the codec-determined
   distribution.

The stored-value distribution does not need to match `D`. It can be
arbitrary. This is convenient: you can build the structure on whatever
subset of `S` you have, with whatever values they actually carry, and
the non-member behavior is still controlled by your codec choice.

## Comparison to padded_codec

`padded_codec<V, M>` allocates patterns 0..k-1 to indexed values and
patterns k..2^M-1 to a designated default. The codespace shares are:

- indexed value at position i: `1 / 2^M`
- default: `(2^M - k) / 2^M`

Useful when you have a small alphabet and one dominant default; the
non-member distribution is biased toward default by `(2^M - k) / 2^M`.

`prefix_codec` generalizes this to non-uniform allocations among indexed
values. If you want indexed value A to be more likely than B for non-members,
`padded_codec` cannot express that (each indexed value has the same share);
`prefix_codec` can, by giving A a shorter prefix.

`dense_codec<V, M>` requires `|V| = 2^M` and gives a uniform non-member
distribution (each value has share `1 / 2^M`). It is the special case
of `prefix_codec` with all codewords of length M.

## Open questions

1. **Does the same property hold for non-ribbon retrievals?** For
   `phf_value_array<MPHF, M>` the answer is no: the non-member output
   is whatever S-member's slot the PHF maps the non-member to. For
   `phf_value_array<padded_phf<MPHF>, M>`, unused slots can be filled
   with any chosen pattern, partially restoring codec control. For
   `xor_filter` / other oracle structures the question is moot:
   non-members are rejected entirely.

2. **Is there a tighter bound than approximate uniformity?** Ribbon's
   linear-algebra analysis gives weak guarantees on the output
   distribution. A more careful analysis might quantify how close to
   uniform the non-member distribution actually is, in terms of the
   span of stored patterns and the band width. Useful for security
   reductions in cipher-map use.

3. **What happens with degenerate stored-value distributions?** *Answered
   in the next section.* The threshold is sharp and geometric: the
   approximation breaks completely when the stored canonical patterns
   span fewer GF(2)^M dimensions than the codec has codespace classes,
   and holds essentially perfectly the moment the span is large enough.

4. **Can a codec be chosen to defeat statistical attacks on the structure?**
   If an adversary has many `(k, lookup(k))` pairs and can distinguish
   them from random, they might infer something about `S`. The codec
   shapes the *output*; what shape leaks the least?

5. **Composable randomized encoding.** `encode_random` is implemented
   in `prefix_codec` but no retrieval currently calls it; the build
   uses canonical encoding by default. Wiring randomized encoding into
   `encoded_retrieval::builder` would spread stored patterns across each
   value's class, which has separate downstream effects on what an
   adversary can recover from the structure.

## When does the codec-controls-output property hold? (storage diversity)

The empirical claim assumed "the stored values span the codespace
broadly enough." This section quantifies that.

### Measurement

`bench_codec_uniformity.cpp` builds `encoded_retrieval<ribbon_retrieval<M>,
prefix_codec>` for many configurations of `(M, codec, storage_diversity)`,
queries 50000 non-members, and reports KL divergence and total variation
distance between observed and codec-predicted output distributions.

Single-machine results at S size = 5000:

| configuration | M | distinct stored | KL div | TV dist |
|---|--:|--:|--:|--:|
| balanced 4-value codec, k=1 | 4 | 1 | 1.39 | 0.75 |
| balanced 4-value codec, k=2 | 4 | 2 | 0.69 | 0.50 |
| balanced 4-value codec, k=3 | 4 | 3 | 0.00002 | 0.0029 |
| balanced 4-value codec, k=4 | 4 | 4 | 0.00013 | 0.0074 |
| skewed 4-value codec, k=1 | 4 | 1 | 0.69 | 0.50 |
| skewed 4-value codec, k=2 | 4 | 2 | 0.34 | 0.25 |
| skewed 4-value codec, k=3 | 4 | 3 | 0.00002 | 0.0029 |
| balanced 8-value codec, k=1 | 8 | 1 | 2.08 | 0.875 |
| balanced 8-value codec, k=2 | 8 | 2 | 1.39 | 0.75 |
| balanced 8-value codec, k=4 | 8 | 4 | 0.69 | 0.50 |
| balanced 8-value codec, k=8 | 8 | 8 | 0.00019 | 0.0076 |

Storage skew on top of codec skew (95% concentration in V0, vs uniform):
identical numbers within sample noise. Storage frequency does not affect
the property; only the set of distinct stored canonical patterns does.

### The threshold is geometric

The KL and TV columns flip sharply between "broken" (TV around 0.5) and
"works perfectly" (TV around 0.005) as `k_distinct` crosses a threshold.
The threshold is determined by the GF(2)^M-dimension of the linear span
of the stored canonical patterns, not by `k_distinct` alone.

**Mechanism**. After Gaussian elimination, ribbon's `solution_` entries
are linear combinations (XORs) of the right-hand-side stored values.
The set of all possible XORs of `solution_` entries is a subgroup of
GF(2)^M generated by the stored values. Any non-member XOR lands in
this subgroup. If the subgroup misses some codec class entirely, that
class gets zero probability in the non-member distribution, no matter
how the codec is configured.

**Numerical sanity check**. For the M=4 balanced codec, canonical
patterns of V0..V3 are `0000, 0100, 1000, 1100`. With k=2 stored
patterns `{0000, 0100}` the span is `{0000, 0100}` of size 2, hitting
only V0 and V1; predicted shares for V2 and V3 are 0. With k=3 stored
patterns `{0000, 0100, 1000}` the span is `{0000, 0100, 1000, 1100}`
of size 4, hitting V0, V1, V2, V3 once each; predicted shares are 1/4
for each. This matches the observed table to within sampling noise.

### Practical guidance

For the codec-controls-output property to hold:

1. **The stored canonical patterns must span all codec classes** when
   viewed as GF(2)^M elements. For a codec with `|V|` indexed values,
   need at least `ceil(log2(|V|))` linearly independent stored patterns.

2. **For balanced codecs with `|V|` values at length log2(|V|)** the
   canonical patterns are equally spaced, and any choice of stored
   patterns that includes at least `log2(|V|) + 1` distinct values
   (including pattern 0) generates the right span.

3. **For skewed codecs** (variable codeword lengths), patterns are not
   equally spaced and the spanning condition is more subtle. In practice,
   storing values with each of the smallest-codeword classes is enough
   (because short codewords have high-bit-position canonical patterns
   that span quickly).

4. **When stored data is not diverse enough**, the property degrades
   gracefully: the non-member distribution is uniform within the span,
   missing only the codec classes the span doesn't reach. Add a small
   number of "seed" keys mapped to the missing values to fix this.

The simplest engineering rule: **if your codec has |V| = K values, make
sure at least K distinct values appear among your stored data**, picking
representatives from each codec class. Then the property holds.

### Cipher-map implications

For oblivious cipher-map use, the codec-controls-output property means
the non-member output distribution is determined by a public choice
(the codec) rather than the private data (S and f). This matches the
desired "structure leaks no membership information" goal.

The diversity threshold is a real prerequisite though. If your
application has a uniform or near-uniform distribution over codec
classes (most realistic), no special handling is needed. If your
application has heavy concentration in one class with the others
empty in S (a defining feature of "store only the interesting cases"),
add seed keys covering each codec class explicitly.

## Source

- Codec: `include/maph/codecs/prefix_codec.hpp`
- Tests / measurements: `tests/v3/test_prefix_codec.cpp`
- Storage-diversity sweep: `benchmarks/bench_codec_uniformity.cpp`
- Prior work in the same direction: `docs/BENCHMARK_RESULTS.md`
  bench_retrieval section; the `encoded_retrieval` discussion in
  `include/maph/retrieval/encoded_retrieval.hpp` doc comments.
