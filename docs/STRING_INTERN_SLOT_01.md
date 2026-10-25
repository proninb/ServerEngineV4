# STRING-INTERN-SLOT-01 — not promoted

Current CPU sampling attributes 1574 exclusive Source samples to
`string_table::intern_prehashed`. Inspection found that a new spelling probes the
index once during lookup, then again during insertion. Experiment: preserve the
first vacant slot and insert there if index capacity has not changed. Rehash uses
the existing insertion routine. Existing spelling comparison, baseline lookup,
String ID order, arena alias protection and allocation rollback remain intact.
No new retained structures or capacity policy changes.

Candidate is compiled only with `CW_STRING_INTERN_SLOT_AB`; production keeps the
original lookup and insertion algorithm. Runtime experimental selector:
`CW_STRING_INTERN_SLOT_REFERENCE=1` reference, `0` candidate. No production env switch.

## Paired full PUBLISH

Same Release+IPO executable, no audit counters. Two excluded warmup pairs followed
by 20 alternating measured pairs. Generic CSV labels: eager=reference, lazy=candidate.

| Metric | Reference median | Candidate median |
| --- | ---: | ---: |
| Total | 1442.665 ms | 1446.930 ms |
| Header | 220.764 ms | 216.879 ms |
| Source | 462.037 ms | 460.155 ms |
| Peak working set | 2573424640 B | 2573703168 B |

Paired total gain: median 7.45 ms, mean 2.439 ms, bootstrap mean 95% CI
[-7.9505, 12.4345] ms; faster 13/20 pairs.
Source: median paired gain 7.7095 ms, mean 5.60385 ms, CI [-2.37885, 13.2571] ms;
faster 14/20 pairs. No outliers removed. Independent medians and paired median
differences are different statistics; neither is substituted for the other.

**Decision:** no reliable full-PUBLISH gain established in this noisy series;
do not promote. This does not prove the algorithm has zero benefit. No further
bridge run or production changes are justified by the current result.
The optimization only removes repeated work on misses, not successful lookups;
sampling at the function boundary does not distinguish those costs.

## Correctness

Both variants passed the compiled/runtime test suite, including BUILD baseline,
string growth, arena aliasing and empty input. Additional experiment fixture inserts
1024 distinct strings with identical supplied hash, then checks exact spellings,
stable IDs and repeated inserts across index growth.
All 44 full PUBLISH calls (including warmups) retained the two known golden hashes
on `build/publish-subsecond-01/fixture/project.json`.

No Graph class or string_table layout/ABI changed. The experiment rebuilds only
string_table.cpp and benchmark.cpp, reusing current production Release+IPO objects
for other sources. In particular, Graph uses accepted Header member-index 4x growth
and original Source link-index 2x growth.

## Reproduction

```powershell
cmake -S experiments/string-intern-slot-01 -B build/string-intern-slot-01 -G "Visual Studio 18 2026" -A x64
cmake --build build/string-intern-slot-01 --config Release -- /m:3
ctest --test-dir build/string-intern-slot-01 -C Release --output-on-failure
python experiments/header-v2-state-lazy-01/run.py build/string-intern-slot-01/Release/StringInternSlot01.exe build/publish-subsecond-01/fixture/project.json build/string-intern-slot-01/new-results --pairs 20 --control-env CW_STRING_INTERN_SLOT_REFERENCE
```

Results: `build/string-intern-slot-01/results-20261023/summary.json`, measurements,
per-call logs and frozen executable. Measured sources and hashes: `candidate-source/`.
