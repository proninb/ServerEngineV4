# RUNTIME-OBJECT-LANES-01 — Object worker limits

2026-10-23. Decision: reject reduced worker limits; retain native capacity (24 on the measured machine). Production defaults are unchanged.

## Hypothesis and scope

RUNTIME-SHM-FAULTS-01 found substantial kernel page-fault/working-set activity beneath `apply_api_one`. Test whether reducing concurrent Object materialization reduces that contention enough to improve full PUBLISH. CPU samples alone do not establish this.

Only the Object worker cap changes: 24 versus 4, 8, 16. Preserve contiguous equal-object ranges, existing scheduler, type API, SHM allocation, link marking and other pools. No pre-touch or large pages. The cap and one coarse Object wall-time probe exist only under `CW_RUNTIME_OBJECT_LANES_AB` in the standalone experiment.

## Method

MSVC Release /O2 with IPO, one executable, UnitProXL fixture at `build/publish-subsecond-01/fixture/project.json`. Reuse complete post-SHA production objects from `build/publish-critical-path-02/symbols`, rebuilding benchmark and project_runtime. Do not reuse these objects after ABI or source configuration changes without rebuilding the production baseline.

For each cap: two excluded warmup pairs, then 20 measured pairs against 24. Rotate candidate order and alternate reference/candidate execution. Total 132 PUBLISH calls. Validate both persisted golden SHA-256 values after every call, outside the measured operation. Separate eight-call resident audit hashes the entire logical SHM after timing and peak-memory capture. No resident hashing in the timing series.

Results: `build/runtime-object-lanes-01/results-20261023/{manifest.json,measurements.csv,summary.json}`. Resident audit: `build/runtime-object-lanes-01/shm-audit-20261023/summary.json`.

## Results

Milliseconds. Each row has its own paired 24-worker control; do not compare separate controls as a worker scaling curve.

| Object workers | PUBLISH control → candidate medians | Median paired slowdown | Objects control → candidate medians | Faster full PUBLISH pairs |
|---|---:|---:|---:|---:|
| 4 | 1298.22 → 1359.92 | 52.06 | 134.13 → 193.56 | 0/20 |
| 8 | 1303.29 → 1333.43 | 32.01 | 132.48 → 168.76 | 0/20 |
| 16 | 1287.93 → 1315.53 | 29.01 | 131.60 → 143.45 | 4/20 |

Mean paired full-PUBLISH slowdowns and bootstrap 95% intervals (10,000 resamples): 4 workers +67.71 ms [53.97, 82.81]; 8 workers +36.65 ms [28.56, 45.23]; 16 workers +29.48 ms [15.68, 43.68]. Objects and resident creation are slower in every measured pair for all three caps. Peak working set remains approximately 2.574 GB, with no demonstrated reduction.

## Correctness

- Release build and `runtime_object_lanes_contract` passed (1/1).
- All 132 timing calls and eight separate audits preserve compiled/runtime golden hashes and expected type/member counts.
- All eight audits preserve all 2,146,276,800 logical resident bytes, SHA-256 `61334dee4dd897389b17658ff54092df60d62c54024856673b4567db60ed9711`.
- SHA verification is test infrastructure, outside production PUBLISH timing.

## Interpretation

Reducing Object concurrency does not improve this workload, despite observed kernel contention. The experiment does not measure whether fewer workers reduce fault counts or contention samples; it establishes that any benefit does not compensate for lost parallelism in wall time. Keep 24 workers on this machine. No production bridge is warranted for losing candidates. The less-than-one-second PUBLISH goal remains unachieved.
