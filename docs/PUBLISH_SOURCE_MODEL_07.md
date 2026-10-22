# Source Model experiment and Source timer correction

The existing experimental `SERVER_ENGINE_SOURCE_MODEL_V2` and
`SERVER_ENGINE_SOURCE_MODEL_DIRECT_V2` options were evaluated together in a
separate Release + IPO build. Both remain OFF by default. This experiment does
not establish a repeatable improvement in complete PUBLISH.

## Comparing the same runs

Full UnitProXL fixture, native execution outside the filesystem sandbox.
The first series contains seven paired uninstrumented PUBLISH runs; the second
contains three paired `publish-stage` runs. Warmups are excluded.

| Series / stage | Baseline mean ms | Model mean ms | Saved ms |
|---|---:|---:|---:|
| Uninstrumented complete PUBLISH | 1435.380 | 1431.057 | 4.323 |
| Instrumented complete PUBLISH | 1451.927 | 1431.663 | 20.263 |
| Instrumented Source (old timer boundary) | 494.586 | 466.987 | 27.599 |
| Instrumented whole semantic pass | 813.459 | 791.176 | 22.282 |
| Instrumented resident construction | 201.424 | 206.514 | -5.090 |
| Instrumented persistence | 202.999 | 203.441 | -0.442 |

The 28 ms Source saving and 4 ms total saving came from different series and
cannot be subtracted to locate overhead. Within the instrumented series, the
complete saving was 20 ms, with about 5 ms lost in resident construction.
These small samples measure variable scheduling, paging and allocation costs;
they do not establish that Source Model causes the resident slowdown.

Code inspection also found an incomplete Source timer: `source_ns` was sampled
before the local parser and predecoded model were destroyed. Their cleanup was
included in `semantic_ns` and complete PUBLISH, but excluded from `source_ns`.
The predecode/setup itself was already included in Source. The unexplained
semantic remainder is consistent with omitted cleanup, but its exact attribution
cannot be recovered from the old timers.

The timer now uses a scope guard declared before parser/model storage. Its
destructor records Source elapsed time after those objects are destroyed,
including on error returns. Replay remains a nested timer excluding destruction.
No parsing, Graph writes or persistence behavior changes.

## Controlled result

Eight alternating ABBA/BAAB blocks, two baseline and two model runs per block,
plus one excluded warmup block:

- Mean paired saving: 7.712 ms.
- Student-t 95% confidence interval: [-7.983, 23.407] ms.
- Five of eight blocks favor the model.

The interval includes a regression, so the experimental options remain OFF.
All 60 complete benchmark runs (including stage runs and warmups) produced
identical compiled.bin and runtime.bin hashes and passed independent baseline
audits. Compiled-project tests, frontend regressions and nine integration
scenarios passed for the experimental build. Frontend regressions also passed
after correcting the timer.

A subsequent full-project run with the corrected timer reported setup 9.867 ms,
replay 456.541 ms and Source total 472.030 ms: roughly 5.622 ms after replay,
predominantly local parser/model destruction. Its independent artifact audit
passed. The normal configuration passed all 22 CTest tests after the correction.

Raw logs, scripts and CSVs are under `build/publish-source-model-07/`.
PUBLISH still skips USN/change tokens and explicit flush; BUILD/REBUILD policy
is unchanged. Subsecond full PUBLISH remains unachieved.
