# PLS10-C11 — String Element Apply Attribution

Diagnostic only. C10 scratch reuse stays enabled; no batching, string lowering,
Graph, queue, Source contract, persistent ABI or production dispatch changes.

Two executables are deliberately separate:

* `PublishStringProfile10C11`: clean C10-derived Source path with opt-in string
  clocks; normal core Graph/identity/allocator. No C9 counters or per-file timers.
* `StringAllocationAudit10C11`: count-only generated Graph/identity copies and
  Source-scoped C++ new/delete observer, extending C10's audit. Timing from this
  executable is never performance evidence.

String context starts after the typed LHS and char-array type have been resolved,
before length validation/path copying/expansion. It therefore measures lowering
and application, not all Source work attributable to a string statement. It ends
after the expanded loop and temporary destruction. No clock is read on each
element by default. Profile mode times one whole string statement, and samples
element processing, Graph path interning, slot apply (including origin callback),
and the two provenance calls independently. Nested estimates overlap and must
not be summed or subtracted to obtain exclusive times.

`PLS10_C11_PROFILE=1` enables clocks; unset disables them. `PLS10_C11_SHIFT=10`
selects 1/1024 independently per metric, `8` selects 1/256. Xorshift streams have
separate seeds per metric; no parent timer forces a child sample. The runner
varies `PLS10_C11_SEED` across blocks. Estimated durations use raw sampled sum ×
sampling denominator and may be distorted by rare growth/preemption; raw sample
counts and ranges across runs are retained. Whole-string clocks include observer
work and are not an uninstrumented production duration.

Allocation contexts partition string-triggered requests into lowering/temporary
work, path interning, slot apply (including origin callback), and provenance.
These are C++ new/delete requests during a scope, not exclusive ownership of
retained Graph buffers. A growth triggered by a string can benefit later
non-string operations. Counts exclude malloc/HeapAlloc/mappings and allocations
before Source. The entire audit still tracks the C10 group partition.

Graph audit records successful intern hits/new insertions separately for string
and non-string Source operations, using existing output and path counts without
a second index lookup. String slot audit uses the existing result's existed and
applied flags. No mutation or new lookup is introduced for attribution.

```powershell
cmake -S experiments/parallel-local-source-10-c11 -B build/parallel-local-source-10-c11 -G "Visual Studio 18 2026" -A x64 -DPLS10_CORE_LIBRARY=C:/Boris/ServerEngineV4/build/parallel-local-source-10-c4/base/Release/PLS08Core.lib
cmake --build build/parallel-local-source-10-c11 --config Release --parallel 4
$env:PLS10_C11_PROFILE='1'
python experiments/parallel-local-source-10-c10/test.py build/parallel-local-source-10-c11/Release/PublishStringProfile10C11.exe build/parallel-local-source-10-c11/Release/StringAllocationAudit10C11.exe build/parallel-local-source-10-c11/fixtures
Remove-Item Env:PLS10_C11_PROFILE
python experiments/parallel-local-source-10-c11/run.py build/parallel-local-source-10-c11/ab-01
python experiments/parallel-local-source-10-c11/analyze.py build/parallel-local-source-10-c11/ab-01
```

Do not build, test or audit concurrently with the timed series. The runner freezes
original C10/profile/audit binaries, verifies native object-storage cold hashes,
then runs 15 rotated/reversed blocks of C10, observer-off, 1/1024, and 1/256.
Persisted artifact hashes match original C10 after every invocation; hashing and
cold scans run outside PUBLISH. All samples are retained. Comparisons measure
regeneration/disabled-observer cost separately from same-binary timing overhead.
This experiment is not a speedup candidate.

The separate `StringClockCalibration10C11` target runs the same sampled empty
scope without PUBLISH. `analyze.py` verifies frozen hashes/count invariants and
runs nine calibration seeds at 1/64 to expose the clock floor. Calibration is
not an automatic correction factor. Very short sampled calls and large scaled
outliers do not establish an exclusive CPU breakdown. Prefer whole-statement
duration, exact counts and an independently measured algorithmic A/B.
