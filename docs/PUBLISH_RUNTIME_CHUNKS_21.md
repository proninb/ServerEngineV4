# Rejected: cyclic runtime object blocks

The existing runtime scheduler assigns each lane one contiguous range with an
equal number of objects. Experiment 21 assigned whole-object blocks of 256 in
round-robin order instead, aiming to reduce imbalance between regions with
different object/array sizes. No object was split, and lanes still wrote only
their own objects. Per-block telemetry was accumulated into per-lane totals.

Compiled-project tests and nine integration scenarios passed. All 24 full-fixture
calls, including warmups, matched both artifact SHA-256 hashes and passed
independent baseline artifact audits. These artifact checks do not by themselves
prove byte-for-byte resident-memory equivalence; no such claim is made.

Seven alternating paired uninstrumented runs averaged:

| Full PUBLISH | Baseline, ms | Candidate, ms |
|---|---:|---:|
| Ordinary | 1398.939 | 1403.299 |

Three separate instrumented pairs averaged:

| Stage | Baseline, ms | Candidate, ms |
|---|---:|---:|
| Full PUBLISH | 1403.813 | 1406.187 |
| Resident construction | 200.680 | 213.282 |

The intended stage regressed by about 12.6 ms. The experiment does not establish
whether scheduling overhead, locality or some other effect caused that
regression. No extended acceptance series was run. The original runtime
scheduler was restored; accepted Source prehash and Header once-state changes
remain in place.

Measurements used Release + IPO without PGO, native execution outside the
filesystem sandbox, and no concurrent builds/tests. Raw source snapshots,
executables, scripts, logs and measurements are in
`build/publish-runtime-chunks-21/`.

After restoration, all 22 CTest tests passed. A fresh PUBLISH matched both
reference hashes, passed the independent baseline audit and left only the two
PUBLISH artifacts in the output directory.
