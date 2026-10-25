# SOURCE-TARGET-CONTEXT-01

2026-10-23. Implemented and semantically validated. **Experiment retained, production default unchanged:** the native candidate improves the Source stage in this series, but neither series establishes a reliable full-PUBLISH improvement.

## Implementation

Source Parser owns one 20-byte context: physical file_id, namespace scope, name string_id, semantic identity_ref WHO, and root type_ref. There is no allocation, pointer into Graph storage, operation buffer or per-page database.

- Scope dispatch checks this key before repeating `find_visible(..., object)`.
- `read_endpoint` reuses the cached root type only for the prebound target with the exact matching key and WHO.
- Every endpoint still resets its path, unit and source range; dependencies, typed member traversal, canonicalization, Graph writes, SourceMap and warnings execute in their previous order.
- RHS operands use the original binding/resolver path and cannot replace target context.
- Root start, declaration entry, namespace entry and namespace exit clear the context. Successful definitions and extern declarations seed it with the already registered WHO/type.
- Type/object name collisions still pass through the existing punctuation lookahead and declarator checks. A cached object spelling cannot turn a declaration into an assignment.

The context stores IDs rather than Graph pointers, so Graph vector reallocation cannot invalidate it. Positive WHO/type facts are reused only within the lifetime and visibility boundaries above. Existing Source extern completion requires the same type. No Header record/member or Graph identity format changes are introduced.

`CW_SOURCE_TARGET_CONTEXT_AB` enables standalone A/B selection via `CW_SOURCE_TARGET_CONTEXT=0/1`. `CW_SOURCE_TARGET_CONTEXT_AUDIT` adds simple owner-thread counters in a separate executable. A native candidate uses only `CW_SOURCE_TARGET_CONTEXT_V2` and has no runtime selector. None of these macros is enabled by root CMake or the production VS project.

## Actual work eliminated

Two full UnitProXL calls per mode with separate diagnostic executable; both goldens match on every call. These diagnostic times are not speed evidence.

| Counter, identical on both calls per mode | Reference | Context |
|---|---:|---:|
| Object-name dispatch lookup requests | 664,309 | 664,309 |
| Lookup requests served by target context | 0 | **529,963** |
| Target endpoint requests | 579,512 | 579,512 |
| Root-type requests served by target context | 0 | **529,963** |
| RHS endpoint requests | 367,144 | **367,144** |
| Successful declaration seeds | 0 | 137,584 |
| Root resets | 3373 | 3373 |
| Namespace resets on this corpus | 0 | 0 |

The 529,963 target type hits are exactly **91.449875%** of target endpoints, matching the declaration-seeded prediction in SOURCE-LOCALITY-01. Name lookup requests also include declaration dispatch, so they have a different denominator. The context eliminates one name-to-WHO lookup and one WHO-to-object-type lookup for each hit; these are not the same operation counted twice.

Raw audit: `build/source-target-context-01/audit-20261023/`.

## Same-executable full PUBLISH

MSVC Release /O2 /Ob2 + IPO. Current accepted post-SHA production objects with Source Parser and benchmark rebuilt. Same validated UnitProXL fixture and physical paths: `build/publish-subsecond-01/fixture/project.json`. No other experimental cache/scratch/runtime feature enabled.

Two excluded warmup pairs, then 20 alternating measured pairs (44 full calls). No growth/hit counters, ETW or concurrent builds. Initial warmups were substantially slower than earlier series; all measured observations, including large deviations, are retained. Artifact SHA checks happen after each lifecycle timer.

| Median, ms | Reference | Context |
|---|---:|---:|
| Full PUBLISH | 1303.690 | 1302.275 |
| Source | 415.957 | 414.0695 |

| Paired statistic | Full PUBLISH | Source |
|---|---:|---:|
| Median gain, ms | **4.545** | **1.219** |
| Mean gain, ms | 3.5595 | 1.9669 |
| Bootstrap mean 95% interval, ms | **[-11.2495, 18.8365]** | **[-6.0596, 9.7473]** |
| Faster pairs | 12/20 | 11/20 |

Both intervals include zero. This series is inconclusive. It is not evidence that the eliminated lookups are a major bottleneck.

Raw series: `build/source-target-context-01/results-20261023/`.

## Native candidate versus frozen original V2

The already prepared native candidate removes A/B flag loads/branches. Its executable contains no `CW_SOURCE_TARGET_CONTEXT` runtime-selector string. Compare it separately against the original frozen post-SHA V2 at `build/publish-critical-path-02/frozen/ServerEngineV4PublishBenchmark.exe`.

Two excluded warmup pairs and 12 alternating measured pairs (28 calls). Same physical fixture and artifact goldens. These results are not pooled with the same-executable series.

| Median, ms | Original production | Native context |
|---|---:|---:|
| Full PUBLISH | 1296.040 | 1287.480 |
| Source | 417.362 | 409.152 |

| Paired statistic | Full PUBLISH | Source |
|---|---:|---:|
| Median gain, ms | **1.875** | **8.817** |
| Mean gain, ms | 3.30833 | 7.46117 |
| Bootstrap mean 95% interval, ms | **[-5.78833, 12.59000]** | **[2.38350, 12.35825]** |
| Faster pairs | 7/12 | 9/12 |

The native Source-stage result is positive in this series. Full PUBLISH remains inconclusive. Independent medians and paired median gains are different statistics; stage medians cannot be added or subtracted to derive a complete-operation gain. Variation in other stages can obscure a small Source saving. No meaningful peak-working-set change is demonstrated; raw memory statistics are preserved. Bootstrap uses 10,000 paired resamples with fixed seed.

Raw native comparison: `build/source-target-context-01/bridge-20261023/`.

## Validation

- Release builds and **4/4 test configurations pass**: reference, context, production without experiment macros, and native candidate. Full frontend suite includes 22 Header V2 semantic contract fixtures.
- Dedicated positive fixture checks namespace shadowing, root transitions, externs, RHS alternation, string overwrites and a warmed type/object spelling collision. Exact ordered Graph initialization/link records, SourceMap contributions/ranges/file indices/dependencies/initialization targets, warnings and origins match control. Independent expected final values confirm outer and shadowed WHO remain distinct.
- Ten negative fixtures compare status, first diagnostic kind/file/byte range/detail and the committed G/SourceMap/warning prefix. Cases include duplicate and conflicting extern declarations, missing objects/members, malformed syntax, string overflow, shadowed members and the existing whole-object const-bool failure.
- All **76 full PUBLISH calls** (four audits + 44 primary + 28 native comparison) preserve compiled SHA-256 `a49506c8df1a999d68624f3a3fd0c317516015b7375deb992ced99736b75522d` and runtime SHA-256 `a0281b3bb48103a7cd339cccd072d24f3524db9932bc67b58a714906996b5ee6`, plus expected type/member counts. Hash checks remain outside measured PUBLISH.
- `git diff --check` and Python runner syntax checks pass.

UnitProXL_V2 and ABB full-PUBLISH compatibility blockers from SOURCE-LOCALITY-01 are not fixed by this experiment. No runtime or speed claim is made for those corpora.

## Decision

Keep the implementation, exact checks and reproducible native target for further review. Do not enable it by default based on these full-PUBLISH measurements. The audit proves the expected locality and reduced operations; the native series suggests a modest Source gain, not a demonstrated complete-operation speedup or a path to sub-second PUBLISH by itself.
