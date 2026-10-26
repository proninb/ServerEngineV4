# PLS10-C8 — Single Binding Lookup

2026-10-25. Decision: **NO ADOPTION**. C7 fast remains the experimental baseline;
production V2 remains selected. The source-level duplicate lookup was removed
correctly, but no meaningful full-PUBLISH improvement was established.

## Isolated change

The normal engine Graph source/header and persistence formats were not edited
by this experiment. C8 generates a diagnostic candidate copy of graph.cpp and
selects between old/new behavior with `PLS10_C8_SINGLE`.

On a rebind miss, the candidate appends using the insertion tail extracted
verbatim from existing add_resolved_link, reusing its capacity preparation,
record append and index placement functions. It does not recheck target absence.
Only the fact of absence is passed forward; no cursor into the old hash table
is reused after capacity preparation/possible rehash. Construction Graph is
mutated exclusively, with no intervening target insertion.

Existing-target replacement and strict add_resolved_link are unchanged. Handle
numbering, overflow checks, target-key validation, rollback and allocation error
paths use the original code. No new API, Graph fields, index/cache, semantic model
or permanent history was introduced. C7 endpoint views, Source semantics, queue,
initialization-slot handling and SourceMap/origin rules were kept unchanged.

Counters are compiled only into a separate audit executable and active only in
the Source backend. Native timed runs have no microtimers or hot counters; the
runner verifies their counters remain zero.

## Correctness and exact structural counts

**768 native C7 oracle runs PASS** across both Graph strategies, requested
1/2/4 workers and reverse dispatch. These reuse the independent Runtime value/
pointer, lifetime-growth, temporal reference, exact warning and error oracles.
Compiled/runtime digests and diagnostics match across strategies.

Dedicated Graph tests verify initial insertion/rebind flags, strict identical
add, conflicting strict-add rejection, invalid operands with reset outputs,
8192 new targets and reverse replacements after growth/rehash. Link handles
remain stable, no replacement appends a record, and both strategies finish with
8193 records and fingerprint `4041119434100471943`.

Cold unmodified UnitProXL audit:

| Source metric | Old path | Single lookup |
| --- | ---: | ---: |
| New bindings | 367,144 | 367,144 |
| Replacements | 0 | 0 |
| Link target lookups | **767,104** | **399,960** |
| Hits | 32,816 | 32,816 |
| Misses | **734,288** | **367,144** |
| Known-absent inserts | 0 | 367,144 |

Exactly **367,144 duplicate absence checks** are removed. These are source/audit
operation counts, not direct native machine-instruction or CPU-cost evidence
under IPO. Compiler optimization and code layout are not inferred from counts.

Original C7, C8 off/single and both audit strategies produce identical
compiled.bin/runtime.bin at the same fixture path. The original V2 image remains
a separate domain. All six cold arms match the full **2,116,048,327-byte object
storage**, including native pointers, defaults and padding:
`cb5aa2607c10f928b273ad8f5d33c29f3656190b3ec9597550cbf068a5f0d0e2`.

## Uninstrumented full-PUBLISH A/B

Release + IPO, original UnitProXL fixture, frozen binaries, 15 rotating four-arm
blocks (**60 timed runs**). Each comparison pairs runs within the same block.
The four arms are original C7, internal C8 off, single lookup and original V2
selected in the original C7 executable. The V2 control does not receive C8.
Cold audits and post-run artifact SHA checks are outside measured PUBLISH.

| Arm | Full PUBLISH median |
| --- | ---: |
| Original V2 | 2109.71 ms |
| Original C7 fast | **2232.08 ms** |
| C8 off | 2247.33 ms |
| C8 single lookup | **2244.20 ms** |

Positive paired gain means the candidate is faster:

| Comparison | Paired gain | 95% bootstrap interval | Wins |
| --- | ---: | ---: | ---: |
| C7 -> C8 off | -4.57 ms | -28.90..+65.56 ms | 7/15 |
| Off -> single | **-6.54 ms** | **-55.57..+34.39 ms** | **6/15** |
| C7 -> single | **-1.44 ms** | **-38.11..+84.05 ms** | **7/15** |
| V2 -> single | -140.27 ms | -152.52..-97.11 ms | 2/15 |

Source ordered-stage medians C7/off/single are 880.039 / 896.559 / 897.551 ms.
Matched internal off->single gain is -5.966 ms (-20.274..+18.236 ms), 6/15 wins.
Thus the stage comparison also establishes no speedup. This is ordered commit
plus waiting, not isolated lookup CPU time; V2 has no comparable experiment timer.

Including resident teardown, C7->single gain is -6.17 ms with interval
-103.50..+100.13 ms. Peak working set versus C7 is inconclusive. No memory
improvement is claimed.

Absolute times varied substantially during this series, including shifts within
blocks. No slow runs were removed. The intervals reflect the retained paired
data; do not treat these medians as a new regression from the historical C7
1499.76 ms. Only fresh matched controls are meaningful here. The result does not
prove the lookup has zero cost, nor statistical equivalence; it proves that this
experiment did not establish the required gain.

## Decision and next boundary

Close C8 without adoption or further refinement. Do not promote a structural
counter reduction into a performance conclusion. No causal claim is made about
whether code layout, IPO common-work elimination or environmental variance
explains the absence of a gain: those were not separately established.

This generic Graph change could also affect V2 if enabled there; such adoption
would require its own direct comparison. No V2+C8 speedup is claimed.

Keep C7 fast as the experimental baseline and leave production unchanged. The
next investigation should return to the complete Source cost difference between
C7 and V2—extraction, representation/ordered resolution and Graph construction—
rather than expanding this binding fast path or introducing another index.
Initialization-slot creation was deliberately not changed or optimized in C8.

Artifacts: `build/parallel-local-source-10-c8/ab-01/{summary,samples}.json`,
frozen executable hashes, cold stdout and `fixtures` outputs. Native build/test/
run logs are `build/parallel-local-source-10-c8-*.log`. Python compilation and
diff whitespace checks pass. Reproduction is in the experiment README.
