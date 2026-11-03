# PLS10-C13 — one-entry string endpoint path reuse

One local experiment on C10. No map/hash/dense endpoint index, batching, slot
callback, origin, Source ordering, Graph ABI or production changes.

V2's actual mechanism is a dictionary keyed by root type, array type and full
typed prefix. That dictionary is not imported. C13 tests a single last-eligible
prefix memo: root type, resolved element type, array bound, complete prefix
(including reserved step bytes), and up to 256 handles returned individually by
the existing Graph interner. There is no assumption of consecutive Graph IDs.
The root+prefix determines array type in the immutable Header domain; the bound
and resolved element type are additionally checked.

Distinct indices inside one string are different paths. A fresh memo cannot
save those initial intern calls. Reuse comes only from later string assignments
to the same typed prefix, possibly on different object WHO values. The key is
checked in `encode()` after existing temporal reference resolution, and the
returned endpoint always uses current resolved WHO. Intermediate reference
encodings with a different value type bypass the memo. Full prefix comparison
runs on each eligible encode; no unsafe statement-global assumption is added.

The memo belongs to one ordered committer and cannot outlive/reset its Graph.
Arrays beyond 256 bypass it. Key changes clear all remembered handles. Vector
capacity is retained; on allocation failure the memo becomes invalid and the
remaining statement falls back to normal interning. Source errors and union
rejection still occur through the existing validation/resolution path.

`PLS10_C13_REUSE=0/1` controls the private experiment (default on). Native counters
are compiled out. Audit verifies every hit against the Graph path's actual root,
resolved type and complete steps without another intern call. The audit is a
separate count/allocation executable; its time is not performance evidence.

```powershell
cmake -S experiments/parallel-local-source-10-c13 -B build/parallel-local-source-10-c13 -G "Visual Studio 18 2026" -A x64 -DPLS10_CORE_LIBRARY=C:/Boris/ServerEngineV4/build/parallel-local-source-10-c4/base/Release/PLS08Core.lib
cmake --build build/parallel-local-source-10-c13 --config Release --parallel 4
python experiments/parallel-local-source-10-c13/test.py build/parallel-local-source-10-c13/Release/PublishStringReuse10C13.exe build/parallel-local-source-10-c13/Release/StringReuseAudit10C13.exe build/parallel-local-source-10-c10/Release/PublishScratch10C10.exe build/parallel-local-source-10-c13/fixtures
python experiments/parallel-local-source-10-c13/run.py build/parallel-local-source-10-c13/ab-01
```

The runner freezes original C10/native/audit binaries and runs 15 rotated/reversed
blocks of V2, C10, memo-off and memo-on. C10 endpoint scratch stays enabled in
all candidate arms. Same-path persisted images match per semantic domain after
each run; whole native object storage is cold-checked before timing. Hashing is
outside PUBLISH. No slow samples are dropped. Do not build or run tests/audit
concurrently with the timed series. Report same-binary off/on separately from
original-C10 comparisons; C13 is not assumed to beat its control.

Tests cover native values/pointers, full fixture bytes/origins/errors across
schedules, changing WHO/subobject targets, differing root types and prefix
alternation. Separate exact C10 comparisons cover 256/257-element arrays and
live memo handles across hundreds of newly created paths/index growth.

Result: **NO ADOPTION**. Only 11776 of 508768 string intern calls are avoided
(2.31%). Same-binary off/on paired PUBLISH gain is -0.82 ms, 95% CI
-19.83–+8.61, 6/15 wins. Original C10 remains the baseline; the private default-on
toggle above is for reproducing this rejected candidate, not a production default.
Full report: `docs/PLS10_C13_STRING_PATH_REUSE.md`.
