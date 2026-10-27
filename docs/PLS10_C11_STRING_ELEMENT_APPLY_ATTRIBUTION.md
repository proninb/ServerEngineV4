# PLS10-C11 — String Element Apply Attribution

2026-10-25. **Diagnostic complete. No batching or production changes.**
C10 scratch reuse remains the experimental baseline; C8 remains NO ADOPTION.

## Scope and reproducibility

Sources and commands: `experiments/parallel-local-source-10-c11/README.md`.
Frozen results: `build/parallel-local-source-10-c11/ab-01/summary.json`,
`samples.json`, `audit.json`, `analysis.json`, and individual stdout/stderr.
MSVC Release + IPO, matching C10 core, same UnitProXL path, four parsing workers,
bounded window eight. Fifteen rotated/reversed blocks compare original C10,
observer-off, sampling 1/1024, and sampling 1/256. Seeds change by block. Builds,
tests and audit/calibration did not run concurrently with timed blocks. No slow
samples were dropped. Hashing and native storage scans are outside PUBLISH.

The count-only audit uses separate Graph/identity copies and replacement C++
new/delete. The profile executable uses normal Graph/identity/allocator and no
C9 per-file timers or hot C7 counters. There is no second endpoint lookup for
attribution. Graph output flags and counts distinguish intern hits/new paths and
existing initialization slots. The algorithm and data representation are intact.

## Exact string volume

| Counter | UnitProXL |
|---|---:|
| String assignments | 15899 |
| Expanded elements / slot applies | 508768 |
| Decoded literal bytes | 116185 |
| Slots beyond decoded literal length | 392583 |
| Existing Source slots on string apply | 0 |
| Successfully applied string slots | 508768 |

On average 32 elements are materialized per assignment. At least 392583 writes
are terminator/padding zero-fill (77.16% of expanded elements); literal bytes
can themselves include zero. This is existing semantics, not evidence that these
writes can be omitted: Header defaults, prior writes and exact provenance must
still be preserved on the general path.

## Allocations: strings are no longer the main source

Scope begins after typed LHS and char-array validation, before length checking,
path copying and expansion. It excludes earlier endpoint/name resolution and
later whole-pipeline cleanup. Requested bytes are allocation traffic triggered
in the scope, not retained ownership: a Graph growth can benefit later operations.
Counts cover C++ new/delete, excluding malloc/HeapAlloc/mappings and pre-Source
allocations. The total Source allocation count remains C10's 993193.

| String context | Calls | Requested bytes |
|---|---:|---:|
| Lowering / temporaries | 31816 | 1282776 |
| Path interning | 47 | 9687015 |
| Slot apply, including slot-origin callback | 61 | 67866084 |
| Provenance calls | 0 | 0 |
| Total | **31924** | **78835875** |

String scopes account for only **3.21%** of remaining Source allocation calls.
Graph/slot growth dominates requested traffic, but there is no allocation per
element: the individual applies mostly use already allocated storage. The
lowering count is approximately two allocations per string statement, consistent
with constructing a typed path vector then extending its capacity; this is not
a claim that every request was identified by allocator stack sampling.

All 12 WHO/Graph operation counters are unchanged from C10, including 1216586
path interns and 705238 initialization slot applies. No optimization was applied.

## Path interning

| Source context | Intern calls | Existing hits | New paths | Failed | Total steps |
|---|---:|---:|---:|---:|---:|
| String elements | 508768 | 472256 | 36512 | 0 | 1526304 |
| Other Source work | 707818 | 405700 | 302118 | 0 | 1300344 |

Strings produce 41.82% of intern calls, with **92.82% hits**. Only 36512 of the
338630 Source-created paths are string-triggered. This confirms repeated lookup
work, but not redundant canonical storage: existing Graph interning already
shares paths. A new index/cache or removing validation is not justified by counts.

## Timing and observer control

Each profile times the whole string lowering/apply statement. Independent random
samples estimate element loop work, path intern, slot apply, and provenance.
There is no clock on every element; samples do not force nested child samples.

| Profile | Whole-string median | Across-run range |
|---|---:|---:|
| 1/1024 | **102.18 ms** | 98.95–111.60 ms |
| 1/256 | **102.97 ms** | 97.60–112.42 ms |

This inclusive scope contains observer work and is not an uninstrumented
production string CPU duration. It is about one fifth of the roughly 490 ms
profiled ordered linking/commit interval, which also includes waiting and other
work. It is not one fifth of all process CPU.

| Native PUBLISH arm | Median |
|---|---:|
| Original C10 | 1422.91 ms |
| C11 observer off | 1412.44 ms |
| C11 1/1024 | 1409.67 ms |
| C11 1/256 | 1419.97 ms |

Same-binary paired observer overhead is **+6.09 ms** at 1/1024 (95% CI
-12.87–25.11 ms), and **+10.42 ms** at 1/256 (CI -0.32–29.78 ms). Ordered-stage
overhead is +3.37 ms (CI -2.69–14.39) and +6.15 ms (CI -1.63–15.19), respectively.
Original C10 vs disabled observer has paired gain 4.61 ms, CI -7.27–15.83.
There is no established performance improvement and no claim of zero overhead.
Medians alone do not determine overhead; the comparisons use paired differences.

## Fine samples are not an exact decomposition

All values below are independently sampled, scaled inclusive estimates.

| Scope | 1/1024 median (range) | 1/256 median (range) |
|---|---:|---:|
| Element loop iteration | 102.91 (91.14–124.62) ms | 101.09 (95.23–104.06) ms |
| Graph path intern | 26.11 (23.24–28.47) ms | 24.73 (22.91–105.96) ms |
| Slot apply + origin callback | 62.16 (52.02–94.41) ms | 60.70 (57.73–115.84) ms |
| Provenance | 22.94 (21.20–176.95) ms | 21.89 (20.38–59.03) ms |

These estimates **must not be added, subtracted or used as exclusive CPU costs**.
Element scope contains the other calls; random sampling, growth and preemption
produce large outliers, all retained. Even the smaller estimates have a clock
floor. A separate identical empty sampled scope, one million opportunities per
seed over nine seeds, has median measured duration **16.24 ns per selected
scope** (seed means 15.39–16.91 ns). At 508768 calls that corresponds to about
8.26 ms of scaled timer floor; at two provenance calls per element, about
16.53 ms. This explains why especially the provenance estimate is not a useful
optimization ranking. The calibration is not automatically subtracted: real
inlining, cache state and preemption differ. Whole-statement timing is much less
sensitive to this per-call floor, but still includes observer work.

## Correctness

**816 oracle runs PASS** with profiling enabled, scratch off/on, fast/fallback,
count audit, different worker schedules, temporal references, strings/duplicate
warnings, negative diagnostics and deep/shallow/root/cross-file lifetime cases.
Exact fixture artifact bytes, diagnostics and origins match across strategies.

Original C10, observer-off, both sampling densities and audit preserve full
UnitProXL compiled/runtime artifact digests after every invocation. Cold native
object storage is identical over 2116048327 bytes, SHA-256
`cb5aa2607c10f928b273ad8f5d33c29f3656190b3ec9597550cbf068a5f0d0e2`.
Frozen binary hashes and counter partitions were checked. Calibration was built
and run after timing; the frozen profile/audit executables were not replaced.

## Decision

C11 establishes a meaningful roughly 102–103 ms inclusive string lowering/apply
area and predominantly hit-based path lookup work. It does **not** establish an
allocation bottleneck in strings or a guaranteed batching speedup. Batching
cannot simply be credited with removing the whole scope: encoding, values,
slot origins, warnings and SourceMap remain required.

A bounded next experiment may examine whether existing batch Graph operations
can preserve stable slot metadata, per-element replacement/warnings and exact
origins without a second endpoint index or extra lookup pass. Only then should
one batching candidate be measured with a fresh uninstrumented full PUBLISH A/B.
If preserving that contract adds comparable work, do not adopt batching.

C10 stays RETAIN; production V2 remains unchanged. No batching candidate or
production acceleration is claimed in this diagnostic stage.
