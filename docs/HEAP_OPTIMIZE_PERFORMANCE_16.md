# HEAP-OPTIMIZE-PERFORMANCE-16

2026-10-30. MEASUREMENT COMPLETE / MEMORY POSITIVE / NO ADOPTION.
No performance acceptance threshold, as explicitly requested by the user.
Production unchanged; PGO OFF; release BLOCKED.

## Design

Four independent real-server processes, balanced ABBA order OFF/POLICY/POLICY/OFF.
Ten warmup cycles then >=600 measured seconds each. Same HEAP-15 executable
`87d8096a00658457ebafebc21477dabbf20cae3fe0084280ea2cbdc6932226af`,
no rebuilding. Same fixed 120635392-byte threshold (115.046875 MiB), fixtures,
connection pattern, state/value/native-SHM/artifact assertions and per-fixture
128 MiB private drift / 16 handle gate. Harness is a separate copy; original
HEAP-15 source and measurements were not overwritten.

TCP PUBLISH/LOAD samples include external wall/monotonic timestamps, latency,
PrivateUsage and process PageFaultCount deltas. API logs remain unchanged.
Conditional PUBLISH rows join the two UNLOAD records from the previous cycle,
distinguishing immediate cleanup from any previous-cycle cleanup. The next LOAD
is additionally grouped by the cleanup in the first UNLOAD of its own cycle.
No mode is switched inside a process.

## Main measurements

UnitProXL TCP latency in ms; throughput is the complete mixed-fixture lifecycle
rate including verification and intentional reconnect work, not PUBLISH-only QPS.

| Run | Mode | PUBLISH median / p95 | LOAD median / p95 | Cycles/s | Unit drift MiB | Measured API sum ms |
|---|---|---:|---:|---:|---:|---:|
| 01 | OFF | 1197.84 / 1427.51 | 212.81 / 239.74 | 1.7643 | +236.94 FAIL | 0 |
| 02 | POLICY | 1184.18 / 1233.59 | 214.72 / 224.72 | 1.8071 | -9.05 PASS | 1287.09 |
| 03 | POLICY | 1192.76 / 1261.16 | 214.10 / 223.54 | 1.7955 | -10.06 PASS | 1186.03 |
| 04 | OFF | 1170.65 / 1229.00 | 212.40 / 218.58 | 1.8418 | +238.77 FAIL | 0 |

Measured cycles: 1061, 1091, 1081, 1111. Elapsed seconds: 601.37, 603.72,
602.06, 603.22. Every process was continuous: maximum adjacent measured
checkpoint intervals were 6.48, 5.61, 6.73, 5.42 seconds. All completed
functional assertions passed. Both POLICY runs passed every fixture's memory
gate. Handles drift zero. All 82 and 78 called API operations succeeded.
No universal guarantee of sufficient decommit or error-path behavior follows.

POLICY API median/p95: 15.92/20.16 ms and 14.74/21.61 ms. API sums are sums of
actual ticks/frequency intervals, not counts multiplied by medians. All calls
in these two processes were measured (none in warmup).

The latency and throughput ranges overlap. This series does not reproduce the
earlier 9.6% lifecycle slowdown, but four chronological processes cannot establish
performance equivalence or attribute the remaining small differences. No
PERFORMANCE PASS/FAIL or statistically significant speedup is claimed.

Unit PUBLISH process-fault medians were OFF 793033 / 790135.5 and POLICY
829596 / 829392. The increase persists. LOAD medians were OFF 542597 / 542601.5
and POLICY 543349.5 / 543351. These include soft and hard faults and unrelated
process activity; they are not hard-fault IO attribution.

## Conditional measurements and missing coverage

All POLICY calls occurred in the first UNLOAD after UnitProXL PUBLISH. None
occurred after LOAD, immediately before the next PUBLISH. Thus the immediate
PUBLISH-after-cleanup group is empty for every fixture. The existing workload
cannot answer that exact causal question; it must not be reported as zero cost.

UnitProXL PUBLISH itself also has zero observations where the preceding cycle
called cleanup: the fixture schedule inserts small projects between Unit cycles.
The next PUBLISH following a cycle with cleanup is a small fixture and has an
intervening LOAD, second UNLOAD and reconnect. Observational results:

| Fixture | Run02 PUBLISH ms, prior cycle no-call / call | Run03 no-call / call |
|---|---:|---:|
| strings | 32.15 / 32.84 | 31.54 / 33.48 |
| inheritance | 8.78 / 9.60 | 8.72 / 9.60 |
| links | 10.49 / 11.38 | 10.54 / 11.44 |

Call groups have 25–28 observations per fixture and always follow UnitProXL;
no-call groups have other prior-fixture mixtures. Raw rows preserve prior fixture
for stratification. These medians do not isolate cleanup from fixture-history
effects and are not randomized counterfactual estimates.

The operation directly after cleanup is UnitProXL LOAD:

| Run | No cleanup: n / median ms / faults | Cleanup: n / median ms / faults |
|---|---:|---:|
| 02 | 28 / 210.66 / 542707.5 | 82 / 215.67 / 543355.5 |
| 03 | 31 / 214.02 / 542709 | 78 / 214.48 / 543357 |

This provides observable post-cleanup LOAD behavior, but unequal group histories
and selection by memory state still prevent causal attribution.

## EACH-02 interruption resolved

System log documents idle sleep at 2026-10-30 10:55:43 and return from low power
at 11:56:22 America/New_York. Power-Troubleshooter event 1 includes sleep UTC
14:55:42.4906238 and wake UTC 15:56:21.3375645. Kernel-Power event 42 says
System Idle; Kernel-General event 1 records clock synchronization after sleep.
This explains the 3640-second gap in HEAP-15 EACH-02. Power settings were not
changed. It remains excluded from continuous performance/soak evidence.
Evidence: `build/heap-optimize-policy-15/system-gap-events.json`.

Prior HEAP-15 actual measured API sums: EACH-04 1950.2443 ms; POLICY-03
1131.4434 ms; POLICY-05 1141.2433 ms. Interrupted EACH-02 635.8678 ms is not
a comparable continuous 600-second run. Results are stored separately in
`build/heap-optimize-performance-16/heap15-API-sums.json`.

## Evidence and remaining gates

`build/heap-optimize-performance-16/comparison.json` contains all per-process,
per-fixture medians/p95, actual sums, throughput, memory gates and conditional
group counts. Each run preserves executed harness, raw operation/sample/API logs,
conditional-publish.jsonl and status. `protocol.json` records the no-new-threshold
decision. Analyzer checks counts, trigger decisions, sequence, exact original
gate recomputation and unchanged executable hash.

No production error policy for failed/insufficient HeapOptimizeResources was
implemented or fault-injected. Direct UnitProXL PUBLISH immediately after cleanup
is not covered by this unchanged fixture schedule. These limitations remain open.
No allocator/backing redesign, PGO training, production integration, commit,
push or deployment was performed.
