# Full Source endpoint reuse: rejected experiments 13–15

Three transient caches were evaluated on the full UnitProXL fixture. Keys used
the exact root type and complete endpoint suffix, with full equality checks.
WHO lookup, declaration-before-use and canonical Graph mutation order remained
on the original Source path. Cached entries retained Source Map dependencies,
resolved steps and the current diagnostic location.

| Variant | Ordinary mean PUBLISH, ms | Candidate mean, ms |
|---|---:|---:|
| 13: bounded unordered map, ordinary token binding | 1434.12 | 1520.28 |
| 14: direct-mapped table, reuse already-bound names | 1424.42 | 1442.38 |
| 15: cheaper delimiter scan and sampled cache index | 1428.58 | 1437.24 |

Each row is a separate seven-pair uninstrumented series with alternating order,
plus excluded warmups. Release + IPO, no PGO, native execution outside the
filesystem sandbox. Three additional instrumented pairs per variant also
showed Source regressions. No builds or tests ran during timing.

The first variant reused 104309 of 946656 endpoints. Later variants reused
450363 and 400793 respectively, but their lookup/preparation costs still
outweighed the work removed. Existing typed member transitions already avoid
most repeated member searches. A complete-path cache adds work on every miss
and cannot share final typed paths across different root types.

All three passed frontend regressions, nine integration scenarios, matching
compiled/runtime artifact hashes and independent full-project audits. The last
variant also passed a new targeted cross-root fixture for inherited members,
arrays, references, string writes, owner dependencies, replacement locations,
comment delimiters and different long names sharing sampled key bytes.

All cache implementations, their semantic-input replay API and cache telemetry
were removed. The semantic regression fixture remains; ordinary Source parsing
and the previously accepted dispatch/timing changes are retained.

Raw logs, measurements and candidate source snapshots are under
`build/publish-endpoint-cache-13/`, `build/publish-endpoint-replay-14/` and
`build/publish-endpoint-probe-15/`. These experiments establish no improvement
in the production path and do not change the MSVC PGO result from experiment 12.
