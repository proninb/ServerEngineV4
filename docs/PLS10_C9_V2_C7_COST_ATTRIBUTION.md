# PLS10-C9 — V2 vs C7 cost attribution

2026-10-25. Diagnostic experiment only. Production V2, Graph ABI, persistence,
Source semantics, C7 queue and algorithms are unchanged. C8 remains NO ADOPTION.

## Method and controls

Frozen results: `build/parallel-local-source-10-c9/ab-01/summary.json`,
`samples.json`, `audit.json`, and individual stdout/stderr files. Runner and build
instructions: `experiments/parallel-local-source-10-c9/README.md`.

Same UnitProXL input, MSVC Release + IPO, matching baseline core, four C7 parsing
workers, bounded window eight. Fifteen alternating original executable V2/C7
pairs establish performance. A separate seven-block series compares original,
regenerated observer-off, and coarse observer-on, per arm. There are no builds
or concurrent test runs during timed series. Each process uses the same input
path; artifact hashing happens after its measured work. No samples discarded.

Coarse scopes are per file, never per operation. GetThreadTimes supplies thread
CPU, GetProcessTimes supplies Source process CPU, QueryThreadCycleTime supplies
thread cycles. C7 extraction numbers sum worker work; they are not stage wall
time. Process CPU and thread CPU overlap. Windows CPU accounting is quantized:
zero for wait/cleanup does not mean those operations are free.

Allocation/WHO/Graph counts come from a separate audit executable, with no
per-call timers. Audit timings are not used to rank production costs.

## Uninstrumented result

| Metric | V2 | C7 |
|---|---:|---:|
| PUBLISH median | 1356.49 ms | 1499.01 ms |
| Full with teardown median | 1658.81 ms | 1815.87 ms |
| Process-lifetime peak working set median | 2571018240 B | 2567843840 B |

Paired C7 regression: **154.45 ms**, bootstrap 95% CI **97.38–170.60 ms**;
C7 wins **0/15**. These are fresh paired results, not comparisons against C8's
absolute times. Peak WS is dominated by the complete process/Runtime and does
not isolate Source. The roughly 3 MiB median difference is not evidence of a
Source memory improvement.

## Coarse profile: additional CPU work, not principally queue waiting

Medians of separate instrumented runs; do not sum them into an exact lifecycle.

| Scope | V2 | C7 |
|---|---:|---:|
| Source wall, including scope destruction | 463.06 ms | 608.10 ms |
| Source process CPU | 453.13 ms | 750.00 ms |
| Source main-thread CPU | 453.13 ms | 546.88 ms |
| Fused per-file V2 parse/semantics/commit wall | 457.77 ms | — |
| Extraction worker wall sum | — | 156.22 ms |
| Extraction worker CPU sum | — | 109.38 ms |
| Ordered apply_file wall | — | 514.57 ms |
| Ordered apply_file CPU | — | 515.63 ms |
| Wait + queue take wall | — | 53.96 ms |
| Processed-model destruction wall | — | 7.46 ms |

C7's apply_file alone is more expensive than the fused V2 per-file path, before
accounting for its extraction workers. Waiting and model cleanup do not explain
most of the difference. Adding parsing workers cannot remove this ordered CPU
work. Exact causal milliseconds per WHO lookup or Graph function are not
established by this coarse experiment.

The observer is **not free**: same-executable observer-on/off paired PUBLISH
overhead for V2 is +28.07 ms (95% CI +7.65–65.37); C7's paired difference is
-1.28 ms (CI -42.94–49.95). Original vs regenerated-off differences have CIs
crossing zero in both arms. Therefore coarse times identify the area to inspect,
not an unbiased decomposition of the 154.45 ms native regression. In particular,
do not subtract measured observer overhead from individual scopes.

## Allocation audit and output volume

Source-scoped replacement C++ new/delete counts, including main and extraction
threads; excludes malloc/HeapAlloc/mappings and allocations made before Source.
Requested bytes are cumulative traffic, not resident memory or retained output.

| Metric | V2 | C7 |
|---|---:|---:|
| Allocation calls | 139270 | 2597813 |
| Cumulative requested bytes | 307745504 | 1051785928 |
| Peak live Source-owned requested payload | 148931630 | 190142364 |
| Extraction allocation calls | fused | 620756 |
| Extraction requested bytes | fused | 580494916 |
| apply_file allocation calls | fused | 1795875 |
| apply_file requested bytes | fused | 440752288 |
| Other Source-scoped calls | included | 181182 |

The audit identifies **1.80 million allocations inside ordered apply_file**.
This is a better next investigation target than another Graph lookup fast path.
It does not by itself prove allocator CPU accounts for all the regression.

C7 emits 717096 local events, reads 5889225 tokens, and cumulatively produces
182960089 bytes of local vector capacity. The maximum capacity of completed
models between extraction and destruction is 6461897 bytes (~6.16 MiB).
This excludes capacity under construction in workers and shared acquisition
tokens; it is not a Source peak-memory estimate. Queue bounds are still effective.
At consumer print time Source-owned allocations have been freed or replaced;
the audit's zero live value there is not a claim that G allocated no memory.

## Identity and Graph work: V2 is already direct, and uses batching

| Source-scoped calls | V2 | C7 |
|---|---:|---:|
| identity_space::find | 1285844 | 1169037 |
| identity_space::resolve | 137584 | 137584 |
| add_resolved_object | 137584 | 137584 |
| intern_resolved_endpoint_path | 760228 | 1216586 |
| Scalar initialization API | 196469 | 0 |
| Batch initialization calls | 15899 | 0 |
| Records passed through batch API | 508768 | 0 |
| Slot callback initialization API | 0 | 705238 |
| Initialization target lookup | 705237 | 705238 |
| rebind_resolved_link | 367144 | 367144 |
| add_resolved_link after miss | 367144 | 367144 |
| Link target lookup | 734288 | 767104 |

Hypothesis that C7 loses because of *more* identity lookups is not supported by
these counters. Identity cost per call still needs separate evidence.

V2's active flags do not enable the Source model/AST path: parsing, resolution
and G writes are fused. Saying V2 has more intermediate stages was incorrect.
V2 expands 508768 string elements through 15899 existing batch calls. C7 lowers
those elements to individual applies and performs 456358 more path interning
calls overall. The counts establish work amplification, not its exact CPU cost
or that all extra path calls are attributable solely to strings. The one extra
C7 initialization is a known contract/materialization distinction; byte equality
of V2 and C7 persisted images is not required.

## Correctness and limitations

All UnitProXL cold checks, including audit builds, preserve each arm's compiled
and Runtime artifact digests. V2 and C7 share full native object-storage SHA-256
`cb5aa2607c10f928b273ad8f5d33c29f3656190b3ec9597550cbf068a5f0d0e2`
over 2116048327 bytes. Hashing and value scans are outside timed PUBLISH.

384 fixture runs PASS: fast/off/audit, 1/2/4 workers, reverse scheduling,
temporal rebindings, exact warnings/origins, negative cases and view lifetime.
An initial audit-output formatting bug appended counters to the last warning;
the fixture correctly rejected scheduling-dependent diagnostic text. Stats now
start on a separate line; the final full suite was rerun successfully.

Frozen performance binaries retain their original hashes. Final rebuilt tools
only add audit accounting and separate diagnostic output lines; they are not
substituted into the frozen native series.

Hardware cache misses/locality were **not measured**. CPU cost of individual
WHO/Graph/provenance functions is not inferred from counts. Source process CPU
does not include earlier acquisition/Header work. No sampling symbol attribution
or cache-locality explanation is claimed.

## Decision

C9 coarse attribution is complete. Keep production V2 and C7's current
architecture. No new Graph index, cache, model, WHO database, or queue.

The next narrow diagnostic target is transient allocation/materialization
**inside apply_file**, especially construction/copying of typed endpoints and
string-element lowering. First identify allocation sites in that scope; select
one candidate only after that evidence. C9 does not justify automatically
porting V2 batching through slot provenance or removing required diagnostics.
