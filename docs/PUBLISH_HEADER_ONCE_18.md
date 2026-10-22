# Avoid whole-project once-state copies during Header reuse

Before every eligible cached Header replay, the preflight copied the current
`#pragma once` state for every file in the project. Most replayed subtrees visit
only a small portion of those files. This added a full file-table scan and hash
lookup per file to every reuse attempt.

Preflight now uses a generation-tagged overlay. A cached once event marks its
file in the current generation. An include first checks that overlay and then
queries the real preprocessor state for its target only. Preflight does not
mutate the real state; ordered replay still commits the events. Each attempt
gets a new generation, including after a failed preflight. Generation wrap
clears the overlay before reusing generation 1.

Include order, root-local provenance, macro eligibility and include-depth
validation remain in the existing replay path. BUILD/REBUILD do not use this
PUBLISH frontend. The optional physical Header model remains disabled.

A diamond-include regression checks that replayed sibling subtrees contribute
their once-only shared leaf exactly once per root, including after the cache
has been populated by an earlier root. Existing tests cover macros, self
inclusion, missing active includes, root-local dependencies and depth limits.

Initial seven alternating paired runs averaged 1391.624 ms baseline and
1379.027 ms candidate. A separate three-pair instrumented series averaged
1405.703 versus 1373.230 ms total, and 223.715 versus 211.815 ms Header. These
are separate series; their differences must not be combined.

Measurements use Release + IPO without PGO, outside the filesystem sandbox,
with no concurrent build or test activity. Every full-fixture run compares both
artifact SHA-256 hashes and runs an independent audit with the baseline binary.
Raw binaries, scripts and logs are in `build/publish-header-once-18/`.

## Controlled full-PUBLISH comparison

The first eight alternating ABBA/BAAB blocks were noisy: mean saving 6.120 ms,
standard deviation 24.340 ms. Because that result was inconclusive, eight more
blocks were added. All samples, including outliers, are retained. The combined
16-block result (two runs per variant per block, plus an excluded warmup block):

| Metric | Result |
|---|---:|
| Baseline mean | 1399.463 ms |
| Candidate mean | 1389.265 ms |
| Mean block saving | 10.198 ms (0.73%) |
| Descriptive Student-t 95% interval | [0.521, 19.876] ms |
| Blocks favoring candidate | 13 / 16 |

The interval describes this adaptively extended series; it should not be read
as a preregistered fixed-sample significance test. The improvement is small and
does not establish subsecond PUBLISH or a new PGO result. The change is retained
because it removes repeated whole-project work and the extended paired series
supports a modest full-operation improvement.

All 92 full-fixture benchmark/warmup calls passed exact artifact comparisons
and independent baseline audits. The nine integration scenarios also passed.
Baseline executable SHA-256:
`5ED93E434BDC134EA5FE57731F095F0CD79CB764441091D60CBF78EF9D8331BA`.
Candidate executable SHA-256:
`8A8CC702FEACD93B9356560658A09250002D04DB48A8F89E03ED6D047523741B`.

The final default build passed all 22 CTest tests, including the new diamond
include regression.
