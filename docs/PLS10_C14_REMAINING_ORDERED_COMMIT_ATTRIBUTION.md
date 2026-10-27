# PLS10-C14 — Remaining Ordered Commit CPU Attribution

2026-10-25. **DIAGNOSTIC COMPLETE.** C10 remains RETAIN. No C13 memo, batching,
new index/model, Source semantic, Graph ABI or production dispatch changes.

## Method and evidence

Commands and scope definitions: `experiments/parallel-local-source-10-c14/README.md`.
Frozen series: `build/parallel-local-source-10-c14/ab-01/summary.json`,
`samples.json`, `audit.json`, per-run logs and binary hashes. Additional cold
counts/calibration: `counter-audit.json`, `analysis.json`. MSVC Release + IPO,
matching C10 core, same UnitProXL path, four parsing workers/window eight.

Fifteen rotated/reversed blocks compare original C10, observer-off, independent
1/1024 and 1/256 sampling. Seeds change per block; no parent forces child samples.
No builds, tests, audit or calibration ran concurrently with timed blocks. No
samples discarded; hashing and cold native scans run outside PUBLISH.

Per-file coarse clocks cover wait/take, apply_file, cleanup and SourceMap roots.
Thread/process CPU are measured only at the Source boundary. Process CPU includes
workers; CPU accounting is quantized. The CPU/wall difference is **not** assigned
to a function. Fine scopes measure sampled inclusive wall duration, not exclusive
CPU samples. Thus this is coarse CPU evidence plus scoped work attribution,
not an ETW per-function CPU profile or hardware-cache locality measurement.

Events are classified before LHS resolution. String events (including typed LHS)
are excluded from fine non-string measurements. Declarations count as non-string.
This is broader string exclusion than C11's post-LHS lowering scope; counts must
not be compared as if scope boundaries were identical.

## Observer cost is measurable

| Native PUBLISH arm | Median |
|---|---:|
| Original C10 | 1397.77 ms |
| C14 observer off | 1399.04 ms |
| C14 1/1024 | 1410.29 ms |
| C14 1/256 | 1408.26 ms |

Paired overhead (positive = observer slower):

| Comparison | PUBLISH overhead, 95% CI | Ordered-stage overhead, 95% CI |
|---|---|---|
| Original C10 → disabled observer | 7.81 ms [0.25, 34.59] | 13.38 ms [10.45, 26.61] |
| Disabled → 1/1024 | 8.84 ms [-12.92, 14.51] | 13.91 ms [7.51, 19.04] |
| Disabled → 1/256 | 12.51 ms [-9.78, 33.25] | 17.08 ms [10.22, 28.81] |
| Original C10 → 1/1024 | 15.33 ms [4.51, 28.00] | 30.32 ms [20.06, 32.42] |

The disabled observer also changes layout/branches; it is not a zero-cost native
control. Do not subtract the table from individual scopes to invent corrected
function durations. These comparisons are diagnostic overhead measurements,
not optimization results. Absolute times are not compared against earlier series.

## Coarse execution

Medians of instrumented runs; do not sum independent medians into a lifecycle.

| Scope | 1/1024 | 1/256 |
|---|---:|---:|
| Source wall incl. scope cleanup | 520.97 ms | 524.81 ms |
| Source main-thread CPU | 484.38 ms | 484.38 ms |
| Source process CPU | 656.25 ms | 703.13 ms |
| apply_file wall | 444.54 ms | 448.77 ms |
| Wait + queue take wall | 48.16 ms | 47.89 ms |
| File-model destruction wall | 1.82 ms | 1.69 ms |
| SourceMap begin/end roots wall | 0.20 ms | 0.20 ms |

There are 3373 apply/wait/cleanup scopes, 6746 root-boundary calls and one Source
scope per run. Ordered apply remains substantial CPU work; queue wait and file
destruction are not its principal explanation. Process CPU includes extraction
and all other process threads active during the window. Its difference from
main-thread CPU is not declared to be exact extraction work, and CPU/wall
differences do not measure a particular resolver or Graph function.

## Exact non-string work

Count-only audit, not timed API calls:

| Scope / operation | Calls |
|---|---:|
| Non-string events | 701197 |
| String assignment events (excluded) | 15899 |
| Declarations / type requests | 137584 each |
| Endpoint requests | 930757 |
| Name helper | 2758075 |
| Symbol binding helper | 1153138 |
| Typed member / index selectors | 1467353 / 52664 |
| Fallback resolve / reference follow | 36083 / 32816 |
| Endpoint encode / Graph path intern | 963574 / 707818 |
| Initialization slot apply | 196470 |
| Rebind expression | 367144 |
| Object registration | 137584 |
| Slot/link origin metadata updates | 563614 |
| Provenance calls | 897668 |
| Warning construction | **0** |

Name helper hits existing local name-ID entries **2405137** times (87.20%);
**352938** misses call string interning, which can itself find an existing global
string. The helper's duration is not string interning time. Type-request cache
hits are zero: the 137584 requests refer to distinct local request records.
That fact alone does not justify another type index: the measured whole scope
is small. Symbol-component total equals symbol-helper calls, so these symbols
are unqualified; qualification parsing is not a demonstrated issue here.

All 12 WHO/Graph operation totals match C10, including full Source counts of
1216586 path interns and 705238 slot applies. Whole-Source C++ allocation calls
remain 993193. No resolution, validation or Graph work was eliminated.

## Independently sampled scopes

Inclusive scaled wall estimates, medians in ms. They overlap and include clocks,
nested observer checks, allocation/growth and possible preemption.

| Scope | 1/1024 | 1/256 |
|---|---:|---:|
| Whole non-string event | 321.33 | 319.00 |
| Declaration event | 35.23 | 35.00 |
| Endpoint preparation | **131.38** | **131.25** |
| Name helper | 105.37 | 99.69 |
| Symbol lookup helper | 73.93 | 70.60 |
| Type request | 8.09 | 8.01 |
| Fallback typed resolve | 9.52 | 9.55 |
| Reference follow | 2.56 | 2.18 |
| Endpoint encode | 65.84 | 61.16 |
| Graph path intern | 53.56 | 53.32 |
| Initialization apply (includes origin callback) | 25.19 | 23.35 |
| Rebind expression (includes argument encodes) | 83.46 | 83.38 |
| Object registration | 5.22 | 4.97 |
| Origin metadata update | 15.16 | 13.39 |
| Provenance | 33.18 | 31.69 |
| Warning construction | 0 | 0 |

**Do not sum these rows, subtract children to infer exclusive durations, or call
them exact CPU costs.** Endpoint includes symbol/name; symbol includes name;
events include declaration/endpoint/commit; fallback includes follow; encode
includes path intern; rebind expression includes both encodes; initialization
apply includes origin policy. No evaluation-order change was introduced to
separate function arguments for timing.

One million empty-scope opportunities over each of nine independent calibration
seeds produce a median empty sampled scope of **16.07 ns**. Scaling this floor
by call volume corresponds to ~44.32 ms for the name helper, ~14.96 ms for
endpoint preparation, ~14.43 ms for provenance and ~9.06 ms for origin metadata.
These are sensitivity illustrations, not corrections to subtract. Real code,
inlining and cache state differ. Short origin/provenance/name/type scopes cannot
be ranked as unbiased function costs from the table.

Large outliers are retained: the 1/256 encode estimate reaches 2244.35 ms, and
the 1/1024 whole-event estimate reaches 1980.42 ms, exceeding the corresponding
pipeline times after scaling a rare slow sample. Their medians are more stable
across densities but do not become an exclusive CPU decomposition. Endpoint
estimates range 122.27–143.77 ms at 1/1024 and 122.83–175.49 ms at 1/256. This
larger scope is a defensible next investigation area; it is not a promised
131 ms optimization budget.

## Allocations and diagnostics

Innermost audit allocation contexts are exclusive even though timing scopes are
inclusive. Counts represent allocations triggered during a context, not permanent
ownership of shared Graph buffers. Excludes malloc/HeapAlloc/mappings and
allocations before Source.

| Non-string context | Calls | Requested bytes |
|---|---:|---:|
| Fallback typed resolve | **141065** | 3411696 |
| Endpoint preparation outside nested helpers | 18029 | 1012520 |
| Declaration outside nested helpers | 13 | 12202510 |
| Name helper | 8 | 23489349 |
| Graph path intern | 36 | 79883445 |
| Initialization apply outside origin callback | 6 | 58752668 |
| Rebind expression outside nested scopes | 51 | 55952992 |
| Object registration | 66 | 7139452 |
| Origin metadata | 41 | 63615613 |
| Provenance | 1 | 12598463 |
| All non-string event contexts | **159316** | **318058708** |

141065 allocations occur in just 36083 fallback resolves (~3.91 per call),
88.54% of non-string-event allocation calls. However, their sampled inclusive
time is only ~9.5 ms. Eliminating them is not established as a large PUBLISH
opportunity; C10's earlier 1.6 million-allocation improvement must not be
extrapolated to this much smaller path.

String event allocations are 31929 / 80933182 bytes, including LHS work. This
differs from C11's 31924 post-LHS lowering allocations by five LHS-triggered
requests. Event partitions exclude extraction and out-of-event file preparation;
their totals are not all Source allocations.

UnitProXL has zero overall conflicts and zero warning calls. Thus diagnostic
string creation does not explain this corpus's hot time. Normal provenance and
origin bookkeeping still executes; it is not equivalent to rare warning text.
Negative/duplicate fixtures establish behavior, not large-project diagnostic
performance. Do not remove provenance or warning semantics based on zero conflicts.

## Correctness and artifacts

**816 oracle runs PASS** with profiling enabled, fast/fallback, scratch off/on,
audit, different worker orders, strings, references, duplicate warnings, negative
diagnostics and lifetime cases. After adding the cold cache/selector census,
the complete 816-run suite was repeated against that audit build and passed.
This is a validation repeat, not additional independent oracle coverage.

Every timed invocation preserves original C10's compiled/runtime digests. Cold
native storage is identical over 2116048327 bytes, SHA-256
`cb5aa2607c10f928b273ad8f5d33c29f3656190b3ec9597550cbf068a5f0d0e2`.
The extra counter audit has its own recorded hash and matches frozen work totals,
artifacts and native storage. Frozen performance executables were not replaced.
Calibration and the extra counter audit ran only after timed blocks completed.

## Decision

C14 closes as a diagnostic, not an optimization or adoption decision. Prioritize
the existing non-string endpoint-preparation path (symbol binding plus typed
member/index construction) for one evidence-backed algorithmic candidate.
Do not credit the name helper with 100 ms of string interning, prioritize warning
text on this conflict-free corpus, or infer a major gain from fallback allocation
count alone. Graph encoding/rebind remains relevant, but its inclusive estimates
overlap and C8 already rejected a simple lookup-count reduction.

No new WHO/type cache, endpoint index, parser architecture, batch operation or
worker-count change follows automatically. Any next candidate needs semantic
parity and a fresh uninstrumented full PUBLISH A/B. C10 and production V2 remain
unchanged; the <1000 ms target is not achieved by this diagnostic work.
