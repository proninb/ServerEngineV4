# PLS09-A — Alias Cost Breakdown

Observation only. No lookup/container/semantic algorithm, ordering, worker count,
Graph format or production lifecycle is changed. BUILD/replay is out of scope.

```powershell
cmake -S experiments/parallel-local-source-09 -B build/parallel-local-source-09
cmake --build build/parallel-local-source-09 --config Release --target PublishAliasPhases09 PublishAliasDetails09 PublishAliasBaseline09 --parallel 8
ctest --test-dir build/parallel-local-source-09 -C Release -R '^PLS09_' --output-on-failure
python experiments/parallel-local-source-09/run.py build/parallel-local-source-09/audit-new
```

The standalone project freshly builds current core via PLS08, generates copies of
the same Source pipeline and adds observation hooks. Original PLS08 and engine
files are untouched. Three frozen executables run the complete native PUBLISH
path on the same private copied fixture:

* `baseline`: unchanged PLS08 candidate, not production Source V2.
* `phases`: coarse alias clocks, including binding catalog copy, endpoint release
  and alias scratch destruction. No per-operation clocks/counters/new override.
* `details`: exact lookup/traversal counters, tracked C++ new/delete requested
  payload, and append-only canonical request descriptors. No per-method clocks.

There are 15 baseline/phase pairs and 5 baseline/detail pairs by default. These
are observer-overhead measurements, not optimization/speedup claims. Full native
PUBLISH and alias interval are both reported. Detailed allocator wrapping changes
allocation layout, and request recording changes memory traffic; its stage times
must not be treated as production CPU costs.

Canonical request reuse is grouped by WHO/path/follow mode only after alias_ms
has stopped, while bound paths still exist. Export/grouping is included in the
full diagnostic operation and therefore in its observer-overhead measurement.
Phase clocks cover alias temporary destruction before their final boundary.
Result arrays that survive alias evaluation are still live at report time.

Every observer first passes all expected values and native pointers against the
independent PLS08 image plus the full object/default storage audit. Every timed
run must preserve exact compiled.bin/runtime.bin digests against the unobserved
same-contract baseline. Cold checks and artifact SHA run outside timed intervals.
The inputs/output files are build fixtures; original projects are untouched.

CTest reuses the already generated 17 PLS08 integration fixture projects and
expected results under build/parallel-local-source-08/writer-fixtures. Run PLS08
tests first on a clean checkout. Both observer targets run actual persistence and
resident SHM oracles. PLS08 unsupported Assign/union/scalar-reference boundaries
continue to apply. No claim of complete arbitrary C++ grammar follows.

Outputs: frozen binaries/hashes, stdout/stderr, per-run phase/counter JSON,
samples and summary. Allocation sizes are requested C++ payload, not process RSS,
heap allocator overhead or OS mappings. Allocations predating alias begin are
outside these counts even if later freed during alias evaluation.
Raw `frees` belong to the original allocation-owner stage, not the stage calling
delete. Use coarse cleanup clocks for destruction cost.

Report: [PLS09-A](../../docs/PLS09_A_ALIAS_COST_BREAKDOWN.md).
