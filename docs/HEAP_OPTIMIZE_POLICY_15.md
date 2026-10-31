# HEAP-OPTIMIZE-POLICY-15

2026-10-30. DIAGNOSTIC MEMORY POSITIVE / PERFORMANCE INCONCLUSIVE / NO ADOPTION.
Production unchanged by this experiment. PGO OFF. Release BLOCKED.

## Protocol fixed before capture

Real ServerEngineV4 TCP lifecycle, four fixtures (UnitProXL, strings, inheritance,
links), ten warmup cycles, then a requested 600 measured seconds per process.
Order: OFF_, EACH, POL_, EACH, POL_, OFF_. All use the same Release+IPO,
PGO OFF executable SHA-256:
`87d8096a00658457ebafebc21477dabbf20cae3fe0084280ea2cbdc6932226af`.

EACH calls HeapOptimizeResources after every successful UNLOAD project.reset.
POL_ calls only when post-reset process PrivateUsage >=120635392 bytes
(115.046875 MiB). OFF_ measures without calling. No cooldown or retuning.
The threshold was calibrated from prior HEAP-14 OFF observations: page-aligned
midpoint of the largest gap between low-cluster maximum 37.16015625 MiB and
high-cluster minimum 192.9375 MiB. It is not a replacement release budget.

The unchanged gate compares last-five versus first-five measured quiescent
medians separately for each fixture: <=128 MiB private drift, <=16 handles drift.
Original artifact, native SHM/value, state, RUN/FREEZE, LOAD, repeated LOGIN and
connection-error assertions were retained. PUBLISH/LOAD/UNLOAD preserve the
healthy TCP connection; intentional disconnect/reconnect follows that lifecycle.
External phase samples measure TCP PUBLISH/LOAD and process PageFaultCount deltas.

## Results

| Run | Mode | Measured cycles | Elapsed seconds | Unit drift MiB | API calls / UNLOADs | Gate |
|---|---|---:|---:|---:|---:|---|
| 01 | OFF | 911 | 604.48 | +237.43 | 0 / 1842 | FAIL |
| 02 | EACH | 351 | 3859.96 | +1.44 | 722 / 722 | PASS preliminary, interrupted |
| 03 | POLICY | 961 | 604.97 | -9.68 | 70 / 1942 | PASS preliminary |
| 04 | EACH | 948 | 600.01 | +2.00 | 1916 / 1916 | PASS preliminary |
| 05 | POLICY | 961 | 600.72 | -0.88 | 67 / 1942 | PASS preliminary |
| 06 | OFF | 1051 | 604.02 | +247.58 | 0 / 2122 | FAIL |

Both POLICY runs passed all four fixture gates. Handles stayed at 88, drift zero.
Every called API succeeded; every trigger decision matched the fixed policy.
POLICY call fractions were 3.6045% and 3.4501%, reducing calls by about 96.4–96.5%
relative to EACH. Counts above include warmup; latency medians below exclude it.

Run 02 has a 3640.34-second gap between adjacent measured checkpoints. Cause is
unknown. Raw data and its computed gate remain recorded, but this run is excluded
from clean continuous-soak/performance evidence. Run 04 supplies the uninterrupted
EACH control. No replacement run was silently added.

| Run | Unit PUBLISH median ms | Unit LOAD median ms | TCP UNLOAD median ms | Unit lifecycle median ms | PUBLISH faults median |
|---|---:|---:|---:|---:|---:|
| 01 OFF | 1662.27 | 242.55 | 0.49 | 6269.55 | 793396.5 |
| 02 EACH, interrupted | 1535.22 | 232.55 | 0.83 | 5933.54 | 831972 |
| 03 POLICY | 1426.73 | 242.01 | 0.44 | 5874.55 | 828917 |
| 04 EACH | 1456.28 | 245.21 | 0.79 | 5936.45 | 831742 |
| 05 POLICY | 1424.86 | 243.22 | 0.39 | 5862.25 | 828691 |
| 06 OFF | 1271.31 | 223.05 | 0.37 | 5350.90 | 793277.5 |

Lifecycle includes validation and reconnect work, not just server execution.
PageFaultCount includes soft/hard faults and concurrent process activity; it does
not establish hard-fault IO or causal attribution. POLICY still shows roughly
35.4k more PUBLISH faults than OFF despite its much lower API frequency.

POLICY medians are close across its two processes and lower than the uninterrupted
EACH PUBLISH median, but OFF changed by about 391 ms between the sequence endpoints.
Thus this series cannot establish a stable latency advantage or preservation of
OFF throughput. It does not prove globally minimal cleanup frequency. There are
only two processes per mode, with one EACH interrupted; cycles are not independent
randomized performance replications.

## Integrity and decision

Analyzer checked all operation/sample/UNLOAD counts, contiguous API sequence,
measurement validity, exact trigger decisions, binary identity and recomputed
original gates. All completed lifecycle/TCP/native/artifact assertions passed;
OFF exit failures were the expected memory gate failures.

Verification confirmed all 287 frozen source files and original OFF, USE and PGD
hashes unchanged. The separate diagnostic source changes only server.cpp and adds
heap_optimize_diagnostic.hpp. Existing unrelated checkout modifications were kept.

Fixed-threshold POLICY is a positive diagnostic memory candidate with far fewer
API calls. Performance remains inconclusive. No production integration, allocator
change, release, PGO training, commit or push is authorized by this result.

## Local evidence

- `build/heap-optimize-policy-15/comparison.json`: full per-run/per-fixture timings,
  faults, gates, call statistics and continuity flags.
- `calibration.json`, `driver-status.json`, `pilot.json`, `verification.json`.
- Each run: `samples.jsonl`, `operation-samples.jsonl`, `unload-timings.jsonl`,
  `status.json`, `server/heap-optimize.jsonl` and console log.
- `experiments/heap-optimize-policy-15/`: preparation, harness, driver, analyzer,
  diagnostic header, verification and preregistered README.
