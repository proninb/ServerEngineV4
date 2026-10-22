# Accounting for full PUBLISH time (38)

## Question and implementation

Header and Source semantics are sequential in the default configuration.
`SERVER_ENGINE_HEADER_SOURCE_OVERLAP_V2=OFF`.
An established reduction of one exclusive phase should therefore reduce
full PUBLISH by the same amount if the other phases remain stable. Historical
means from separate benchmark series cannot establish that relationship.

Added `publish-timeline`: coarse phase clocks with per-token Source counters
and Header member publication counters disabled. Existing publish-stage still
collects these counters; publish-profile additionally collects hot input clocks.
Ordinary publish passes no telemetry. Timeline retains the outer Header/Source
timers and measures full operation duration, context setup, warning/topology
work, persistence transition and temporary-context destruction.

The last timer is declared before the construction context and finishes after
the context is destroyed, including Graph, strings, identities and file/lexical
storage. The existing resident timer ends before that destruction. The new
operation timer ends after local destruction and is compared with the caller's
total. Unaccounted time is reported rather than hidden in a phase.

Header physical preparation is separate from header_ms. In the default path,
semantic_ms comprises Header physical preparation + Header semantic replay +
Source + finalization + semantic_other_ms. These nested fields must not also
be added to semantic_ms when calculating the top-level total. Persistence
subtimers can overlap internally and likewise are not additional top-level
phases.

## Controlled comparison

Compare ready+words+batch (baseline) against ready-only+batch (candidate),
using the same current code and identical compiler settings. Four calls per
block: each variant in ordinary publish and publish-timeline. Order rotates
and reverses; one warmup block is excluded, eight blocks are measured.
Both golden hashes and an independent baseline audit are checked after every
call. No builds or tests run during timing. Timeline runs assert that Source
token counters and Header member counters are zero.

Ordinary full-operation saving is the primary statistical endpoint. Source,
remaining time and per-phase intervals are diagnostic, not simultaneous
confidence claims. In each timeline pair, total saving equals Source saving
plus the change in all other time; the analysis reports both explicitly.

First diagnostic run: total 1400.34 ms, accounted 1400.31 ms, unaccounted
0.0203 ms, caller overhead 0.0057 ms. Temporary destruction was 21.8967 ms.
This single run establishes accounting coverage, not an optimization gain.

The complete default build passed all 22 CTest tests. Raw data, executables,
analysis and scripts: build/publish-timeline-38/.

## Results: representation comparison

Eight measured pairs per mode and variant; thirty-two measured calls total,
plus four excluded warmups. The primary ordinary-PUBLISH means were
1386.626 ms baseline and 1373.248 ms candidate: saving 13.379 ms, paired
Student-t 95% interval [2.174, 24.584] ms. This is a modest gain in this series;
it is compatible with experiment 36's smaller estimate and wider interval.
These are separate series, not a before/after comparison of their absolute
means. This diagnostic investigation does not change the production defaults.

| Timeline phase, ms (nested semantic rows marked) | Baseline | Candidate |
| --- | ---: | ---: |
| Total | 1389.060 | 1372.530 |
| Source (within semantic) | 432.795 | 429.469 |
| Header semantic (within semantic) | 226.487 | 220.518 |
| Header physical (within semantic) | 78.672 | 78.274 |
| Other semantic work + finalization (within semantic) | 12.016 | 11.134 |
| Initial Source lexical preparation | 178.339 | 176.984 |
| Persistence | 203.586 | 203.848 |
| Resident runtime publication | 197.373 | 194.257 |
| Temporary context destruction | 22.145 | 21.052 |

The actual timeline Source saving was 3.326 ms, diagnostic 95% interval
[-7.353, 14.005] ms. The previous 18.347 ms stage saving in 36 came from only
two instrumented samples per variant and was not replicated here. It was not
an established reduction that then disappeared at the end of PUBLISH.

Within these same timeline pairs, total saving 16.530 ms equals Source saving
3.326 ms plus 13.204 ms change in all remaining time. Remaining phases vary
too; individual phase means are descriptive and do not prove that changing
Source representation caused a Header or runtime speedup. No disappearing
critical-path time is observed.

Maximum unaccounted operation time over all sixteen measured timeline calls:
0.0142 ms. Mean caller overhead is about 0.0037 ms. Mean timeline-minus-ordinary
differences were +2.434 ms baseline and -0.718 ms candidate; their intervals
include zero. The series does not establish a large timeline observer cost.

## Observer control: same executable, stage versus timeline

A separate excluded warmup block plus eight mirrored ABBA/BAAB blocks compared
publish-stage against publish-timeline using the identical baseline executable.
Sixteen measured calls per mode, with SHA/audit after every call.

| Stage minus timeline | Mean | Paired 95% interval |
| --- | ---: | ---: |
| Full operation | +14.392 ms | [-8.136, 36.920] ms |
| Source | +6.373 ms | [-0.071, 12.818] ms |
| Semantic overall | +8.026 ms | [-0.723, 16.775] ms |

Stage counts all 5,889,225 Source tokens; timeline counts zero. Counter code
is demonstrably disabled in timeline, but this series does NOT establish that
counter overhead accounts for the whole historical discrepancy: the intervals
include zero. No specific background process, scheduler, I/O or CPU-frequency
cause was established either. Do not promote those possibilities to findings.

## Conclusion

There is no observed loss of an established Source gain at a downstream barrier.
The historical two-sample stage estimate was treated too strongly; it was not
a stable phase speedup. The latest controlled experiment has only a 3.3 ms
Source difference, while other time varies by comparable or larger amounts.
Full ordinary PUBLISH did show a modest 13.4 ms benefit in this new series.

Use ordinary PUBLISH as the performance endpoint and publish-timeline for a
paired explanation of its changes. Keep physical Header preparation and
temporary destruction in the accounting. Do not compare isolated stage means
from different series or add nested/overlapping timers. A confidence interval
including zero does not prove absence of a small benefit.

All 72 full calls across the two new series (including excluded warmups) kept
the golden compiled/runtime hashes and passed independent audits. No production
token representation defaults were changed. The endpoint cache from 37 remains
removed. Whitespace validation (`git diff --check`) passed.
