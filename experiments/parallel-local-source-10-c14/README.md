# PLS10-C14 — Remaining Ordered Commit CPU Attribution

Diagnostic only on C10. No C13 memo, string batching, algorithm/Graph/ABI/queue
changes or production dispatch changes. C10 scratch stays enabled in all timed
arms. Two executable roles separate normal-allocator timing from count audit.

`PublishCommitProfile10C14` wraps a clean C10 path. Profile off reads no per-call
clocks and increments no fine counters. Profile on uses per-file coarse clocks
for wait/take, apply_file, model cleanup, SourceMap roots, and the whole Source
scope. Thread/process CPU are read only at the Source boundary (not per call or
per file); process CPU includes extraction workers. Windows CPU is quantized.
Differences between CPU and wall are not assigned to a particular function.

Events are classified before LHS resolution. String assignment events, including
their LHS work, are excluded from fine non-string metrics. Declarations and
namespace events count as non-string. Each fine metric has its own RNG stream
and sample decision; parents never force children. A metric's calls are counted
only when profiling or in the separate audit. No clock runs on every fine call.

Non-string scopes: whole event; declaration; endpoint request; name; symbolic
lookup; type request; fallback temporal resolve; follow; encode; path intern;
initialization slot apply; link expression; object registration; origin metadata;
SourceMap/provenance; warning construction. Scopes are inclusive and overlap.
Link expression includes its argument encodes and Graph rebind, preserving
existing argument evaluation; it is not exclusive rebind CPU. Origin scopes
include initialization callback and link sidecar update. Whole event includes
other scopes; endpoint includes symbol/name, resolve includes follow, and slot
includes origin. Never sum/subtract sampled estimates into a partition.

The count-only `CommitAllocationAudit10C14` adds Source-scoped C++ new/delete
observation and unchanged C10 WHO/Graph counters. Allocation phase guards form
an exclusive innermost-context partition during events; time scopes remain
inclusive. String events are counted separately. Extraction and out-of-event
work are excluded from this fine allocation partition but remain in the parent
C10 audit totals. Triggering Graph growth does not establish retained ownership.
Counts exclude malloc/HeapAlloc/mappings and allocations before Source.

```powershell
cmake -S experiments/parallel-local-source-10-c14 -B build/parallel-local-source-10-c14 -G "Visual Studio 18 2026" -A x64 -DPLS10_CORE_LIBRARY=C:/Boris/ServerEngineV4/build/parallel-local-source-10-c4/base/Release/PLS08Core.lib
cmake --build build/parallel-local-source-10-c14 --config Release --parallel 4
$env:PLS10_C14_PROFILE='1'
python experiments/parallel-local-source-10-c10/test.py build/parallel-local-source-10-c14/Release/PublishCommitProfile10C14.exe build/parallel-local-source-10-c14/Release/CommitAllocationAudit10C14.exe build/parallel-local-source-10-c14/fixtures
Remove-Item Env:PLS10_C14_PROFILE
python experiments/parallel-local-source-10-c14/run.py build/parallel-local-source-10-c14/ab-01
python experiments/parallel-local-source-10-c14/analyze.py build/parallel-local-source-10-c14/ab-01
```

`PLS10_C14_PROFILE=1` enables clocks, unset disables them. Shift 10/8 means
independent 1/1024 / 1/256 sampling; seed changes across runs. The runner freezes
original C10, observer and audit binaries and uses 15 rotated/reversed blocks.
It checks same-path persisted hashes after every run and cold-checks the entire
native object storage. Hashes/scans are outside PUBLISH. All samples retained;
no concurrent builds, tests, audit or calibration during timed blocks.

The standalone empty-scope calibration uses the same sampler with no PUBLISH.
Calibration is a floor diagnostic, not a correction formula. Fine timings can
include nested observer cost, rare growth or preemption; amplified outliers are
retained. Small estimates near the clock floor are not useful CPU rankings.
Use original vs observer-off and same-binary off/on before interpreting data.
