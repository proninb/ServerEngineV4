# RUNTIME-FIRST-TOUCH-01

2026-10-23. Decision: reject separate Object pretouch. Production remains unchanged: native 24 Object workers on this machine, first-touch during real construction.

## Scope and safety

Follow-up to RUNTIME-SHM-FAULTS-01 and RUNTIME-OBJECT-LANES-01. Same executable, MSVC Release /O2 + IPO, 24 Object workers in both modes. Candidate adds a separate 24-lane pretouch pass immediately before Objects, after canonical materialization and static-link marking.

`shm_layout_builder` appends live objects after common storage. Preparation scans compiled object slots for the minimum valid object offset; the touched tail ends at `shm_layout::size()` and is checked against mapped bytes. The pass touches the first Object byte and one byte at each subsequent system-page boundary. Pages are split into disjoint contiguous ranges. Each volatile byte is read and written back unchanged, preserving existing link markers. Padding in the Object tail may also be touched. No blanket zeroing, new type index, large pages, or scheduler change.

The experiment reuses `CW_RUNTIME_OBJECT_LANES_AB`, with `CW_RUNTIME_OBJECT_PRETOUCH=0/1`. No production build option enables it. Worker startup/join, boundary preparation, and pretouch all remain within resident creation and full PUBLISH timing. `object_phase_ms` measures only subsequent initialization; `pretouch_ms` is reported separately.

## Method and results

Existing UnitProXL fixture `build/publish-subsecond-01/fixture/project.json`; same physical path in both modes. Complete post-SHA production objects from `build/publish-critical-path-02/symbols`, rebuilding benchmark and project_runtime without ABI changes. Two excluded warmup pairs followed by 20 measured alternating pairs; 44 timing calls. No concurrent builds, resident hashing, or sample removal during timing.

| Stage, median milliseconds | Control | Pretouch |
|---|---:|---:|
| Full PUBLISH | 1302.875 | 1353.645 |
| Resident creation, including pretouch | 192.5345 | 247.5655 |
| Objects alone | 132.643 | 85.90345 |
| Separate pretouch | 0 | 101.9565 |

Full PUBLISH median paired slowdown: **49.38 ms**. Mean paired slowdown: 47.2305 ms; bootstrap 95% interval [38.196, 55.7115] ms (10,000 paired resamples). Candidate is faster in only 1/20 full-PUBLISH pairs. Resident creation is slower in 20/20 pairs, median paired slowdown 55.608 ms. Objects alone improve in 20/20 pairs, median paired gain 46.9397 ms, but the extra pass costs more than it saves. Independent stage medians are not additive decompositions of median PUBLISH.

The touched Object tail is 2,116,156,616 bytes. Peak working-set medians are 2,573,910,016 versus 2,576,814,080 bytes; no memory reduction.

## Validation

- Release build and `runtime_first_touch_contract` passed (1/1).
- Both artifact goldens match after all 44 timing calls and four separate resident audits.
- All four audits match the full 2,146,276,800 logical resident bytes, including FIXED_DIRECT references: SHA-256 `61334dee4dd897389b17658ff54092df60d62c54024856673b4567db60ed9711`.
- Audit hashing runs after the measured lifecycle and peak-memory capture; it is absent from timing runs and production.
- `git diff --check` and Python syntax compilation passed.

Raw data and source/executable hashes: `build/runtime-first-touch-01/results-20261023/{manifest.json,measurements.csv,summary.json}`. Separate audit: `build/runtime-first-touch-01/audit-20261023/summary.json`. Reproduction: `experiments/runtime-first-touch-01/README.md`.

This measures wall time, not fault counts or fault latency. It confirms that moving first-touch into a separate pass does not improve this workload. It does not contradict the earlier compiled-image persistence first-touch optimization: that addresses a different mapping and a serial encoder. No production bridge is warranted for this losing candidate; the sub-second PUBLISH goal remains unachieved.
