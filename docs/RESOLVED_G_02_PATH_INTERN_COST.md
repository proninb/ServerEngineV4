# RESOLVED-G-02 — PATH-INTERN-COST

Date: 2026-10-24. **No production adoption.**

## Decision

No implementable candidate met the requested >=10% improvement in complete Graph replay. The best candidate, single-probe plus cached rehash hashes, improved the paired replay median by **14.7978 ms / 5.2027%**, 15/15 pairs faster. Its full-PUBLISH paired gain was **23.53 ms**, but the 95% interval **[-2.55, +53.65] ms** includes zero.

Keep the existing production Graph path interner. Do not turn an oracle reservation result into a production speed claim. The next priority is measurement of Source resolution/endpoint formation/provenance, with Graph unchanged.

## Input and correctness

The exact RG01 UnitProXL stream: 1,477,848 resolved API calls, including 760,228 path requests:

- Existing canonical path: **389,550** requests.
- New canonical path: **370,678** requests, appending **839,508** steps.
- Final path capacity: 466,609 records; step capacity: 1,005,310 records.
- Final index: 1,048,576 slots.

All **306** full Graph replays passed exact canonical-handle/final-record/original-index parity. There are 270 measured runs and 36 warmup runs; warmups also check every API result/replacement flag. Auxiliary hash values are checked independently after the timer. This adds no per-operation timing probes.

The small fixture additionally proves overwrite/rebinding behavior, extern completion, string batches and deliberate-corruption rejection. Its CTest passes all nine replay cases and ordinary PUBLISH with each of the three implementable candidates.

## Paired full-replay results

15 measured pairs per case, two warmup pairs excluded. Positive gain means faster than the adjacent control. These are avoided-work experiments, not exclusive CPU timings for a single function.

| Case | Kind | Paired gain, ms | Paired gain, % | 95% interval, ms |
|---|---|---:|---:|---:|
| Query hashes already available | Oracle | 11.5507 | 4.14% | [8.60, 19.30] |
| Reserve path/step vectors | Oracle | 11.3712 | 3.97% | [9.49, 16.17] |
| Reserve final-sized path index | Oracle | 20.9932 | 7.27% | [18.54, 28.29] |
| Both reservations | Oracle | 36.6962 | 12.58% | [29.71, 38.51] |
| Single probe for lookup/insertion | Candidate | -1.8779 | -0.67% | [-7.24, 3.76] |
| Cache old path hashes for rehash | Candidate | 10.3296 | 3.61% | [8.74, 23.64] |
| Single probe + cached hashes | Candidate | **14.7978** | **5.20%** | **[11.70, 16.74]** |
| Skip existing-path calls | Oracle | 29.1007 | 10.23% | [24.40, 32.81] |
| Prebuilt final path catalogue | Oracle | 129.3900 | 45.44% | [124.74, 132.48] |

The combined practical candidate's independent medians are control 286.44 ms and candidate 271.15 ms; the paired gain is 14.80 ms. New capacity for cached hashes is **3,732,872 bytes (~3.56 MiB)**. The original records/index contents remain identical, and the auxiliary array is not persisted.

Existing-only bypass estimates the cost of the entire repeated-request path, including validation/hash/lookup. It does not isolate lookup CPU. Prebuilt-path bypass makes all path metadata available early and omits its copying from `g_ms`; it is only an avoided-work bound. Subtracting 29.10 from 129.39 does **not** establish an exact new-path cost.

Reservation allocations are inside `g_ms`, but final counts/capacities are supplied for free. Production would have to obtain those estimates without a costly prepass or excess memory. The 12.58% oracle result does not pass the practical-candidate gate.

## Independent full PUBLISH

Best implementable candidate selected from replay, then 15 new paired PUBLISH measurements after two warmup pairs:

| Metric | Control median | Candidate median | Median paired gain | 95% interval |
|---|---:|---:|---:|---:|
| Complete PUBLISH | 2,190.630 ms | 2,172.800 ms | 23.530 ms | [-2.550, 53.650] ms |
| Source stage | 815.497 ms | 801.563 ms | 23.648 ms | [4.885, 30.535] ms |

Complete PUBLISH was faster in 10/15 pairs; Source in 12/15. There is a small Source improvement in this series, but complete-PUBLISH improvement is not established and the replay acceptance target was missed.

All **36 successful full UnitProXL PUBLISH calls in the primary series** (one warmup, one capture process, 34 full-PUBLISH control/candidate calls) retained:

- compiled.bin: `a49506c8df1a999d68624f3a3fd0c317516015b7375deb992ced99736b75522d`
- runtime.bin: `a0281b3bb48103a7cd339cccd072d24f3524db9932bc67b58a714906996b5ee6`

Digests are external checks after each process; they are not measured production work.

## Timing calibration and limits

Historical RG01's 153.3827 ms replay / 424.027 ms Source are not used as fresh controls. Absolute times varied with current execution/launch conditions. The same frozen RG01 executable produced replay samples 266.50–273.34 ms through the current Python launch check and 157.79–162.78 ms in a short direct native check. The cause was not established; no CPU/power settings were changed.

A separate 10-pair calibration compared the unmodified production executable with RG02's inactive control under the same harness. Source medians: **812.7925 vs 808.702 ms**; paired inactive-minus-pure difference **-5.484 ms**, interval **[-12.151, 3.331] ms**. Thus the overall absolute slowdown was also present in the unmodified executable; inactive control cost was not resolved in that series.

A short direct-native RG02 replication (three measured pairs per case) gave the combined candidate **5.40%**, with control median 252.79 ms and candidate median 238.95 ms. It supports the below-10% outcome, not a universal absolute timing claim.

Use only the contemporaneous within-process replay pairs and independent same-executable PUBLISH pairs for the decision. Interactions between hashing, occupancy, allocation and caches make ablation gains non-additive. No claim is made that the remainder of Source time belongs entirely to Parser or SourceMap.

## Reproduction

Commands/contracts: `experiments/resolved-g-02/README.md`.

Raw results: `build/resolved-g-02/measurements/{summary.json,replay-summary.json,replays.csv,publish.csv}` and individual stdout/stderr; calibration in `build/resolved-g-02/calibration`; short native replication in `build/resolved-g-02/native-replication.{log,json}`.

All changes to Graph behavior and the auxiliary field are behind `CW_PATH_INTERN_02`, defined only by the standalone experiment target. Root production PUBLISH remains unchanged.
