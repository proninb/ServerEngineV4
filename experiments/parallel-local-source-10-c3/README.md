# PLS10-C3 — Parallel Parsing / Ordered Direct Commit

Experimental native PUBLISH path with the **temporal reference contract**:
traversal uses the binding at the current Source operation. Later rebinding
never moves an earlier write. This supersedes the final-bindings expectations
of PLS02–PLS10-B; those fixtures are not an oracle for this changed contract.

```powershell
cmake -S experiments/parallel-local-source-10-c3 -B build/parallel-local-source-10-c3 -G "Visual Studio 18 2026" -A x64
cmake --build build/parallel-local-source-10-c3 --config Release --parallel 4 --target PublishOrderedSource10C3
ctest --test-dir build/parallel-local-source-10-c3 -C Release -R '^PLS10C3_' --output-on-failure
```

Select `PLS08_CANDIDATE=1` for this path, `=0` for the existing production V2
path in the same executable. `PLS10_WORKERS` selects 1..32 extraction workers
(default 4, capped by file count). The window is twice the actual worker count.
`PLS10_REVERSE_TASKS=1` reverses dispatch within each bounded window for tests;
it introduces a test-only window barrier. Production dispatch is ascending.
`PLS10_TRACE=1` emits exact conflict origins outside the PUBLISH timer.

Planner uses declaration-order DFS through project.json/groups/nested Projects.
Workers own local syntax records and never mutate G, identities or String Table.
One persistent pool fills a bounded ring; commit consumes one file at a time
in planner order and each file's events in extraction order. Consumed models
are destroyed before the window advances. No operation sort, winner ordering
or project-wide operation buffer exists. Source tokens are shared read-only
and released only after all workers join; the window bounds parsed models,
not total input-token memory or the size of a single file.

The generated commit adapter reuses existing type checks/name resolution from
the PLS02 typed producer, but instantiates no bound_model. Its only endpoint
scratch is reused and cleared after each Source event. Values use C2's Graph
slot callback, accepting each operation immediately. Reference rebinding
updates G immediately; traversal reads its existing link target index through
`graph::link_at_target()`. Values and links keep only previous origin by slot,
and duplicate warnings are emitted in commit order. No final-binding rounds,
separate value reducer, value replay or Writer is used.

Objects used by a file must be explicitly declared or extern in that file.
An unbound reference traversal/source fails at that operation; future bindings
are not borrowed. Const declaration values keep the native cold construction
path. Member defaults remain separate from explicit Source writes. Union
storage, Assign inputs and noncanonical single-member seed endpoints fail closed.

Tests: 22 cases x 4 scheduling modes = 88 native PUBLISH checks, including
independent SHM values/native pointers, exact warning origins, nested Project,
queue wrap, defaults, arrays, enum/const, inheritance and negative diagnostics.
compiled.bin/runtime.bin hashes must match across scheduling modes. Hashes are
checked after execution, never inside timed PUBLISH.

`run.py OUTPUT` freezes the native executable and copies the historical
UnitProXL fixture for cold audits followed by a possible paired V2 comparison.
That historical fixture currently fails the required file-local declaration
rule. The optional `--explicit-globals` records a private-copy S/SW adaptation;
it is diagnostic only and still fails on further undeclared cross-file names.
Do not treat these failed preflights as completed A/B or benchmark results.
Neither mode edits original projects. Full UnitProXL speedup is not established.

Timing labels inherited from PLS08: linking_ms is ordered commit + waiting,
overlapping extraction. extraction_ms=0 means no separate extraction wall timer,
not free parsing. Alias/Writer are absent; their work must not be inferred by
subtracting historical timers. Native total_ms remains the full PUBLISH timer.

Production dispatch, persistence formats and BUILD/REBUILD/LOAD are unchanged.
