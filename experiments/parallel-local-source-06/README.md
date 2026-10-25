# PLS06 — Dense Write Heads

Single candidate: replace only the write-head unordered map with a dense
`uint32_t` array indexed by the existing canonical cell ID. Flat interning,
PLS04 early release, binding heads, reference linking, next chains, LWW,
provenance and final sorting stay intact. Production is unchanged.

```powershell
cmake -S experiments/parallel-local-source-06 -B build/parallel-local-source-06
cmake --build build/parallel-local-source-06 --config Release --parallel 8 --target PLS06
ctest --test-dir build/parallel-local-source-06 -C Release --output-on-failure
python experiments/parallel-local-source-06/run.py build/parallel-local-source-06/measurements --pairs 15
python experiments/parallel-local-source-06/summarize.py build/parallel-local-source-06/measurements
```

Both arms are in one executable: `PLS06_DENSE=0/1`, always with
`PLS03_MODE=1;PLS04_EARLY=1`. No per-operation timers. Both retain the empty
map object in the wrapper; candidate's default empty-map allocation overhead
is included. Dense storage grows only on demand to `cell + 1`, initializing
holes with `UINT32_MAX`. It supports cells assigned during binding, later
first writes to such cells, sparse IDs and empty write streams.

Dense traversal visits occupied slots in cell order. Original per-cell event
sorting and final value/diagnostic sorting remain. Any cheaper sorting/locality
is a consequence of the new index traversal and must be reported by stage.
Statistics are captured at traversal boundaries, without per-write counters.

The normal executable retains Release+IPO. The allocation audit main uses
`/GL-` (MSVC allocation-observer LTCG ICE) and PLS04 allocation hooks. Audit
payload/counts are separate from normal timing/RSS. PLS05 ownership logging is
not enabled. `PLS06_HEADS_PATH` saves slots/capacity/population metadata;
`PLS04_MEMORY_PATH` and `PLS03_PROFILE_PATH` retain PLS04 memory/coarse clocks.

The runner requires the existing UnitProXL fixture/oracle and frozen PLS05
normal executable, using paths in `run.py`. It performs normal/audit oracle
checks in both modes and orders, two warmups, five calibration pairs against
the frozen baseline and 15 same-executable AB/BA pairs. Use a fresh output dir.
`--calibration-only` runs a separate five-pair baseline/control confirmation,
without rerunning or replacing the candidate series. The recorded confirmation
used `build/parallel-local-source-06/calibration-confirmation`.

Working-set sampling, exact process peak, allocation payload and lifecycle
boundaries match PLS04. Header/lexical preparation, oracle and report I/O are
excluded from semantic lifecycle; token/local/IR release and fingerprint
consumption are included. This is not production PUBLISH or G materialization.

Existing fixtures run normal/audit × map/dense; additional sparse fixtures
check late preexisting cells, hundreds of gaps, alias duplicate histories and
no writes, with full value/warning/origin/fingerprint comparison in both orders.
