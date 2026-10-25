# SOURCE-LINK-INDEX-01 — 4x growth rejected

Only the existing Graph link-target index was changed in the experiment.
Production retains its original 2x capacity routine and insertion code through a
compile-time branch; the candidate requires `CW_GRAPH_LINK_INDEX_AB`.
Header member-name index remains on its previously accepted 4x policy.
No endpoint/initialization index, WHO, Source semantics or G format changes.

## Diagnostic counts on UnitProXL

| Counter | Reference 2x | Candidate 4x |
| --- | ---: | ---: |
| Ensure calls | 367144 | 367144 |
| Growths | 18 | 10 |
| Scanned old slots | 1048568 | 699048 |
| Rebuilt entries | 524284 | 349524 |
| New entries | 367144 | 367144 |
| New insertion probes | 640040 | 555836 |
| Rehash probes | 611416 | 374680 |
| Maximum insertion probe | 37 | 37 |
| Cumulative requested slots | 2097144 | 2796200 |
| Final capacity | 1048576 | 2097152 |

Counts are from a separate atomic-counter build; its times are not used.
Fewer rehashes do not imply less total allocated memory: this corpus ends on a
different capacity boundary for the two policies.

## Counter-free paired full PUBLISH

Same Release + IPO executable, 2 excluded warmup pairs then 20 alternating pairs.
Frontend/Source settings are identical; only `CW_GRAPH_LINK_INDEX_REFERENCE` changes.
CSV `eager` means reference 2x and `lazy` means candidate 4x (generic runner labels).

| Metric | Reference median | Candidate median |
| --- | ---: | ---: |
| Full PUBLISH | 1449.265 ms | 1450.315 ms |
| Source | 465.259 ms | 465.914 ms |
| Header | 214.094 ms | 214.968 ms |
| Peak working set | 2573774848 B | 2590228480 B |

Paired total gain median **0.225 ms**, mean 2.1295 ms, bootstrap mean 95% CI
**[-7.586, 11.7985] ms**, candidate faster **10/20** pairs.
Source paired gain median -0.471 ms; mean 1.02625 ms, CI [-7.9061, 9.78435] ms.
Peak WS medians differ by +15.69 MiB. No time outliers excluded.
The series has more environmental noise than earlier Header measurements;
it establishes no reliable time benefit and does not prove exact equivalence.

**Decision:** reject production adoption: no demonstrated full-PUBLISH benefit,
with twice the final link index capacity. No bridge series is justified after the
primary comparison failed. Do not extrapolate the Header growth result to Source.

## Correctness and reproducibility

All 44 timing PUBLISH calls plus both diagnostic calls preserved the known compiled
and runtime golden SHA-256 at the original isolated fixture path.
The compiled/runtime suite ran twice in one test executable, once for each policy;
both passed. Existing growth fixtures cover 1024 new targets, stable link handles,
duplicate add, conflicting source rejection, rebinding and initialization replacement.
Candidate run forces one larger-allocation failure, verifies the flag was consumed,
and completes the suite through the smaller-allocation fallback.

Other than graph.cpp and benchmark.cpp, targets reuse immutable current-v2 Release
objects from the sampling build. Graph class layout/ABI is unchanged. Timing target
has no audit counters or allocation-failure hooks.

```powershell
cmake -S experiments/source-link-index-01 -B build/source-link-index-01 -G "Visual Studio 18 2026" -A x64
cmake --build build/source-link-index-01 --config Release -- /m:3
ctest --test-dir build/source-link-index-01 -C Release --output-on-failure
python experiments/source-link-index-01/run_audit.py
python experiments/header-v2-state-lazy-01/run.py build/source-link-index-01/Release/SourceLinkIndex01.exe build/publish-subsecond-01/fixture/project.json build/source-link-index-01/new-results --pairs 20 --control-env CW_GRAPH_LINK_INDEX_REFERENCE
```

Use a new audit destination when repeating (the script refuses overwrite).
Raw data: `build/source-link-index-01/audit/counts.json`,
`results-20261023/summary.json`, `measurements.csv`, full per-call logs and frozen
executable. Exact measured source snapshots/hashes: `candidate-source/`.
