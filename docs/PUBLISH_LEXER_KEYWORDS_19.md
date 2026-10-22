# Rejected: skip keyword hashes for long identifiers

The lexer scans identifier bytes, then computes their FNV hash to classify
keywords. The longest supported keyword is `reinterpret_cast` (16 bytes).
Experiment 19 skipped the hash for longer identifiers when no file-local symbol
stream was requested. Symbol-producing Header preparation retained its original
hashes. No new cache or semantic shortcut was introduced.

The candidate passed frontend regressions and nine integration scenarios.
Every full-fixture benchmark/warmup call compared both artifact SHA-256 hashes
and passed an independent audit using the baseline executable.

Seven alternating paired uninstrumented runs averaged 1397.969 ms baseline and
1390.504 ms candidate. Three separate instrumented pairs averaged:

| Stage | Baseline, ms | Candidate, ms |
|---|---:|---:|
| Full PUBLISH | 1403.077 | 1394.767 |
| Lexical preparation | 166.753 | 170.280 |

The intended stage did not improve. The small total difference cannot establish
a benefit from this lexer change. No extended acceptance series was run; the
candidate was removed and the original lexer restored. The accepted Header
once-state optimization from experiment 18 remains in place.

Measurements used Release + IPO without PGO, native execution outside the
filesystem sandbox and no concurrent builds/tests. Raw binaries, source
snapshots, scripts, logs and measurements are in
`build/publish-lexer-keywords-19/`. No new speedup or subsecond result is claimed.
