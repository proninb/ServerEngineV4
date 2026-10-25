# PLS08-C — Native Full PUBLISH A/B

2026-10-24. Result: **correct on the checked compatible domain; NO ADOPTION for
performance**. No production PUBLISH/BUILD/REBUILD/LOAD source was changed.

## What was measured

`PublishFinalState08` uses a generated copy of the current native parser
orchestrator and native publish benchmark, compiled MSVC Release + IPO against
the current core. A runtime selector in one executable chooses:

* CONTROL: unchanged Source Parser V2, including its existing Header/Source
  preparation overlap.
* CANDIDATE: declaration-order DFS planner, sequential local extraction,
  global semantic linking, alias-aware final-cell reduction, owned final state,
  existing resolved Graph Writer, warnings and native SourceMap finalization.

Both use native acquisition, Header V2, artifact persistence (compiled.bin and
runtime.bin), Runtime layout/planning, actual resident fixed-direct SHM and
construction cleanup. Candidate disables unused V2 Source predecode overlap;
it does not execute both parsers. No new workers, Graph ABI or identities.

Only PUBLISH CLI modes are accepted. The input is a private copied fixture under
build; existing `.serverengine` artifacts are excluded from the copy. No original
project output was replaced. Resident SHM names include process ID; the configured
fixed address is the same in both arms. BUILD acceleration capture is off, with
no source.bin/database.bin/manifest artifacts, USN capture or persistence flush.

The candidate excludes Assign and any Source path traversing union storage.
This conservative supported-domain guard runs without a diagnostic Runtime
allocation/range scan. The richer A/B diagnostic backend still tests the actual
overlap rejection. There is no physical-order winner.

## Correctness gates, outside paired timers

Before warmup/repetitions the runner:

1. Runs the deep PLS08-A/B writer/query/SourceMap/image oracle on the copied
   project, producing a verified expected compiled image.
2. Runs actual candidate PUBLISH and compares all 705,238 values and 367,144
   bindings by semantic endpoint keys, plus their actual resident bytes and
   native pointer addresses, against that expected image. Raw Graph handles
   are not assumed equal across the two backends.
3. Separately hashes every object's full storage (WHO/type/offset/size followed
   by bytes) in native CONTROL and CANDIDATE. This includes untouched Header
   defaults, arrays, reference slots and padding in 137,584 objects. All
   2,116,048,327 storage bytes matched:
   `cb5aa2607c10f928b273ad8f5d33c29f3656190b3ec9597550cbf068a5f0d0e2`.

This last comparison covers object storage, not the entire resident image's
process-specific runtime bookkeeping. Fixed-base pointer representations match
in the checked configuration. New-contract conflict fixtures are independently
specified in B and are not required to equal old V2 semantics.

Deep verification, SHA, per-value scans, history fingerprints and artificial
zero-initialized ~2 GiB vector storage are absent from timed repetitions.
Normal native SHM creation/initialization remains inside PUBLISH. Each arm's
persisted compiled/runtime digests are checked AFTER child operation/teardown
timers, and remain stable across all its repetitions. Cross-arm compiled/runtime
byte parity is not required: the new contract/record representation differs,
including one explicit declaration initialization on UnitProXL.

## Paired result

Final frozen executable SHA-256:
`72905d54bf7d32f621e00140d689e40c6a5563a657674cf7e9f5b66f50b41d0c`.
Archived executable, raw samples, logs and summary:
`build/parallel-local-source-08/publish-c-final/`.
After this series, only the cold SHA verifier's hash-buffer destruction lifetime
was strengthened and exercised in the native fixtures. The archived executable
above remains the exact timing artifact; that cleanup is outside timed PUBLISH.

One warmup per arm, then 15 alternating-order pairs in separate processes. Same
fixture, executable, settings and environment; no concurrent experimental
benchmark was launched. The runner verifies the frozen executable hash again at
the end. Bootstrap intervals below use 20,000 resamples of paired differences.

| Metric | CONTROL median | CANDIDATE median | Median paired regression |
|---|---:|---:|---:|
| Native PUBLISH operation | 1,884.42 ms | 3,090.05 ms | +1,197.66 ms |
| Operation plus resident teardown | 2,250.39 ms | 3,462.70 ms | +1,178.43 ms |

Native-operation paired regression 95% percentile bootstrap CI:
**+1,114.91…+1,216.11 ms**. Candidate faster in **0/15** pairs.
Operation-plus-teardown regression CI: +1,087.38…+1,244.67 ms.

`total_ms` uses the existing native publish operation boundary, which includes
construction-state cleanup but leaves a ready resident Project alive. The second
metric adds measured resident Project destruction/unmapping. It excludes the
intervening report formatting/cold-check interval; it is a sum of those two work
durations, not whole process wall time. Process wall duration is recorded
separately in samples.

Peak working set is dominated by native resident SHM: medians 2,573,873,152 vs
2,571,915,264 bytes. Paired reduction is only about 2.03 MiB, not a meaningful
architectural reduction of the ~2.4 GiB process peak.

Absolute durations varied in this session, including lower late-pair values.
Do not compare these independent medians directly to historical ~1.3 s runs.
The within-pair result is unambiguous, and the separate phase profile below
also observes the same direction at a lower absolute baseline.

## Where the cost remains

Candidate coarse-stage medians from the final timed series:

| Stage | Median |
|---|---:|
| Source task planner | 17.02 ms |
| Local extraction and token release | 236.39 ms |
| Semantic linking and local release | 437.23 ms |
| Alias/reduction | 781.78 ms |
| Guard, writer, warning projection | 388.23 ms |
| Final output/result release | 6.12 ms |

Medians are not additive. Alias/reduction is still the largest candidate stage;
linking and writer are also substantial. No optimization was mixed into this
integration experiment.

One separate phase-only profile per arm, after the timed pairs:

| Native stage | CONTROL | CANDIDATE |
|---|---:|---:|
| Operation | 1,348.72 ms | 2,144.34 ms |
| Previous-artifact cleanup | 8.53 ms | 8.08 ms |
| Configuration/acquisition | 24.26 ms | 23.75 ms |
| Source lexical acquisition/preparation | 145.96 ms | 148.94 ms |
| Header physical preparation | 77.13 ms | 77.85 ms |
| Header semantic execution | 224.63 ms | 224.46 ms |
| Source including final writer | 428.72 ms | 1,233.87 ms |
| SourceMap finalization | 1.53 ms | 1.45 ms |
| Persistence | 200.93 ms | 195.49 ms |
| Resident Runtime/SHM | 200.66 ms | 195.84 ms |
| Construction-state transient release | 21.53 ms | 20.67 ms |

These are **single diagnostic profiles**, not stage medians or paired confidence
intervals; they must not be summed with the paired series. They locate the added
cost in the new Source route, rather than indicating an approximately one-second
native encoder or Runtime cost inferred from the old deep diagnostic driver.

## Decision

PLS08-B validates integration for the documented supported projection. PLS08-C
now supplies an actual complete native PUBLISH measurement; it does not support
production adoption for speed or a subsecond claim. Keep native V2 as production
baseline. Preserve final-state semantics, standalone writer, cold independent
oracles and the reproducible full-path benchmark for future focused work.

No compact ABI, extra workers, Runtime rewrite, BUILD/replay or speculative
optimization was added to hide this negative performance result.

Reproduce with a new output directory:

```powershell
cmake --build build/parallel-local-source-08 --config Release --parallel 8
ctest --test-dir build/parallel-local-source-08 -C Release --output-on-failure
python experiments/parallel-local-source-08/run_publish.py build/parallel-local-source-08/publish-c-new --pairs 15
```

The preliminary `publish-c-01` series also had 0/15 candidate wins, but the final
series above is the authoritative result with frozen binary and complete object
storage/default parity gate. Do not pool the series.
