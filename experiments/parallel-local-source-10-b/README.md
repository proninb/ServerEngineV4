# PLS10-B — Sparse Indirect Writes

Isolated PUBLISH experiment. Production V2, Graph API/ABI and PLS10-A2
binding reduction remain unchanged. This does not implement PLS10-C.

After existing typed validation, direct writes retain one current winner per
logical direct cell. Only reference-dependent writes remain pending in full.
Losers retain origin/event metadata, not values. Finalization resolves pending
targets through final bindings and applies Source-order max to each cell.
Warnings merge direct/indirect events into the original replacement sequence.

A shared index assigns canonical IDs lazily in the old finalization order;
pre-discovering direct winners does not change cell numbering. The writer and
remaining bound columns still use the previous experimental implementation.
Typed paths/endpoints are not removed from the producer in this slice.

## Reproduce (PowerShell, repository root)

```powershell
cmake -S experiments/parallel-local-source-10-b -B build/parallel-local-source-10-b -G "Visual Studio 18 2026" -A x64
cmake --build build/parallel-local-source-10-b --config Release --target PublishSparseWrites10B SparseWritesAudit10B SparseWritesSemantic10B SparseWriteTests10B
ctest --test-dir build/parallel-local-source-10-b -C Release -R '^PLS10B_' --output-on-failure
python experiments/parallel-local-source-10-b/run.py build/parallel-local-source-10-b/ab-01 --pairs 31 --v2-pairs 15
```

Use a fresh output directory for each frozen series. Do not run builds/tests
concurrently with timing. The runner freezes binaries, copies its fixture,
checks semantics/origins/query/native pointers and full object-storage SHA
outside timed runs, and checks compiled/runtime file digests after each run.
Outputs include samples.json, summary.json and cold allocation profiles.

Selectors: `PLS10_FINALIZE=2` selects A2 in both arms;
`PLS10_WRITES=0` is the retained-write control, `=1` is sparse candidate.
`PLS08_CANDIDATE=0` selects production V2 in the same executable.
Allocation audit covers extraction, typed production and alias finalization;
its instrumented times are not performance evidence. Native runs omit that
observer. Full lifecycle including teardown is reported separately.

Extraction remains sequential in this harness. This is a typed-input kernel
experiment, not a new complete file-local TYPE/extern frontend. Existing
unsupported union overlap / Assign inputs remain fail-closed. No BUILD,
REBUILD, LOAD, persistent format or production dispatch changes are included.
