# SOURCE-V2-STRING-BATCH-01 / SOURCE-STRING-BATCH-AB-01

2026-10-26. **Existing production batch path confirmed beneficial.** Current
checkout already defaults `SERVER_ENGINE_SOURCE_STRING_BATCH_V2=ON`, defining
`CW_SOURCE_STRING_BATCH_V2`. No new production algorithm or enablement is needed.
Keep this setting. No Parser, Graph, production CMake or binary-format change
was made during this experiment. C10, direct mmap and a new pipeline are excluded.

## What is compared

Current V2 + Fast Reader, RAM G, existing string path reuse unchanged. A private
source snapshot compiles both existing string commit branches into one native
Release executable, selecting scalar versus batch once per string statement.
Scalar uses the existing per-element commit_construction / SourceMap sequence.
Batch collects resolved records and calls G.add_resolved_initializations(), then
publishes SourceMap in the same element order. No new cache or Semantic Model.

The initial branch was already present and active in production: these numbers
are a scalar-versus-existing-batch counterfactual, **not additional speedup over
the current batch-enabled version**.

## Full UnitProXL native result

Windows 11 build 26200, MSVC 19.51 x64 Release /O2 /GL /LTCG, PGO OFF, IPO ON.
Runner explicitly launched unsandboxed using require_escalated. Exact same
original UnitProXL project, ordered project.json and source files, shared output
artifact directory, cleaned environment, separate process per operation.
15 alternating native pairs after warmup. No detailed timers, Graph counters,
allocation observer or fault hooks in the native timing binary. No concurrent
build/test work, no discarded samples; all hashing outside PUBLISH timing.

| Metric | Scalar | Batch |
|---|---:|---:|
| PUBLISH median | 1370.42 ms | 1331.39 ms |
| Peak process working set median | 2572095488 B | 2572013568 B |

Marginal median difference: **39.03 ms (2.85%)**. Median paired scalar-minus-
batch gain: **35.09 ms**, bootstrap 95% CI **21.27–39.96 ms**, wins **13/15**.
20,000 resamples of pair differences, seed 110. Marginal and paired medians are
different statistics. Peak-WS paired CI spans zero: no demonstrated RAM benefit.

Separate seven-pair phase-only profile, not the primary timing evidence:

| Median | Scalar | Batch |
|---|---:|---:|
| Full PUBLISH | 1343.43 ms | 1301.09 ms |
| Source | 455.463 ms | 429.512 ms |
| Semantic | 785.571 ms | 759.431 ms |
| Persistence | 202.003 ms | 202.334 ms |

Profile total paired gain 33.01 ms, CI 7.16–47.72 ms, wins 6/7. Source paired
gain 26.958 ms, CI 25.511–30.816 ms. Source is part of Semantic; these medians
are not additive or a decomposition of the native series.

## Graph operations — separate counter executable

Counters are disabled at compile time in the native executable. Separate audit
runs before/after timing have identical counts and native artifact/SHM parity.

| Counter | Scalar | Batch |
|---|---:|---:|
| String statements | 15899 | 15899 |
| String elements | 508768 | 508768 |
| Individual Graph initialization API calls, whole PUBLISH | 705237 | 196469 |
| Batch Graph initialization API calls | 0 | 15899 |
| Individual Graph API calls within strings | 508768 | 0 |
| Batch Graph API calls within strings | 0 | 15899 |
| Low-level initialization upserts | 705237 | 705237 |
| Low-level string upserts | 508768 | 508768 |
| Initialization index capacity preparation calls | 705237 | 212368 |
| Endpoint path intern calls | 760228 | 760228 |
| Endpoint interns within strings | 36512 | 36512 |

Batch removes 492869 repeated per-record API/capacity-preparation calls. It does
not remove the 508768 element records or low-level slot updates. Existing path
reuse already avoids most repeated string path interns; this experiment does
not introduce or change that cache.

## Correctness and failures

- 81 fixture operations pass. Full compiled.bin, runtime.bin, PUBLISH SHM,
  LOAD/audit SHM and formatted sorted diagnostics match across arms.
- Tests include inherited/nested arrays, alternating bounds and lengths,
  concatenation, escapes and embedded zero, terminator/padding, maximum fitting
  literal, string/scalar overwrite order, duplicate warnings, multiple Source
  roots, repeated writes through rebound references and reference chains.
- Errors tested: overflow, wide literal, invalid escape, non-char destination,
  missing semicolon and reference-type mismatch. The currently accepted unbound-
  reference case is checked for parity, not reclassified as a new error.
- compiled_project and frontend_regressions CTest suites pass in both scalar
  and batch modes. Existing frontend tests check actual expected byte values,
  zero padding and warning counts. Existing Graph tests verify stable replacement
  slots, input order and invalid middle endpoints rejected before mutation.
- In the separate audit build, inject an error on the 17th string upsert, after
  16 writes, in both modes. Verify failure, null public Project, no persisted
  artifacts and successful recreation of the same SHM name/fixed address.
  A pre-reserve failure is also injected for batch. These are explicit injected
  failure statuses, not an exhaustive allocator/OOM campaign or a promise of
  rollback of internal discarded Graph/SourceMap prefixes. No partial Project
  is published. Fault hooks are absent from the native and production builds.

Full-corpus bytes match across all 44 timed observations and correctness/audit
operations. Common SHA-256:

- compiled.bin: `b30bdd279af2472096243b99360d02ac70e6337af556c1a9f33739ea5fbafb67`
- runtime.bin: `a0281b3bb48103a7cd339cccd072d24f3524db9932bc67b58a714906996b5ee6`
- logical SHM: `61334dee4dd897389b17658ff54092df60d62c54024856673b4567db60ed9711`

Compiled byte equality covers canonical G, numbering and SourceMap; runtime
equality covers physical plans. The complete input, source and executable
manifests remain unchanged across the timing series.

## Reproduction and decision

Sources/commands: `experiments/source-string-batch-ab-01/README.md`.
Results: `build/source-string-batch-ab-01/` retains source snapshot/manifests,
native and audit executables, logs, 30 native / 14 profile observations,
summary.json, counts.json, checks.json, parity.json, verified.json, CTest logs
and functional-results.json. Git base: 87abe00. Pre-existing local PGO options
and resolver audit definitions were copied but remain inactive (PGO OFF and no
resolver audit macro).

Native SHA-256: `bf626fa2d54943586a3ad925ac17a6c5ee5bf947f4d45d518d98cc9aca21daf3`.
Audit SHA-256: `1262cf3b13dceb1f9c49530bcf64c3083032ece44889e8252be45739071cb0ca`.

Adoption condition passes for the existing batch path: reliable full PUBLISH
benefit and parity. Production already uses it, so retain the existing ON
default. The private toggle/counters/fault machinery is not integrated. No new
production optimization or further hot-path algorithm is claimed or started.
