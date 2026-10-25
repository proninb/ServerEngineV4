# PUBLISH-NO-SHA256-01 — accepted

## Contract and active consumers

Fresh PUBLISH now acquires immutable owned Header/Source/configuration bytes without
computing content SHA-256 or BUILD change tokens. It also omits the configuration
and preprocessor digests used for reusable BUILD lineage. PUBLISH does not persist
that lineage. Graph WHO/String IDs, SourceMap and compiled/runtime encoding are unchanged.
Identifier hashes in the lexer/string index are unrelated and remain intact.

The previous ETW profile was correct: SHA-256 really ran inside PUBLISH acquisition.
A separate diagnostic build counted **5436 hash_file_content calls / 116539185 bytes**
for UnitProXL. Candidate counted **0 calls / 0 bytes**; both outputs retained golden hashes.
Counter code is compiled only with `CW_FILE_HASH_AUDIT`, never used for timing.

`acquire_file_content` retains hashing by default for existing callers. Its new
explicit `calculate_content_hash` argument defaults true. Only the fresh PUBLISH
File Context propagates false through acquisition jobs. BUILD/REBUILD/default
snapshot and streaming-proof APIs retain exact SHA-256 behavior.

Unhashed repeated acquisitions compare the actual owned bytes; absent digests are
not used as proof of equality. `calculate_content_hash` rejects an unhashed context
with `unsupported` rather than returning a reusable aggregate proof of zeroes.
PUBLISH configuration proof objects remain transient and are not saved as BUILD lineage.
Snapshot consistency/metadata checks and same-handle reading remain unchanged;
this experiment removes hashing only, not post-read observation checks.

## Same-executable A/B

Release + IPO, no hot-path clocks/counters. Two excluded warmup pairs, 20 alternating
measured full-PUBLISH pairs. Both variants retain accepted lazy Header state and
Header member-name index 4x growth. Source link index remains 2x. Other recent
unproven candidates remain off.

| Metric | SHA reference | No-SHA candidate |
| --- | ---: | ---: |
| Full PUBLISH median | 1345.76 ms | 1308.05 ms |
| Initial Source acquisition/lexical median | 178.98 ms | 141.15 ms |
| Peak WS median | 2573703168 B | 2573703168 B |

Primary paired total gain median **41.735 ms**, mean 38.609 ms;
bootstrap mean 95% CI **[28.3695, 48.1975] ms**, faster **18/20 pairs**.
The difference of independent medians is not the paired median gain.
Header/Source semantic variations are diagnostic; no independent semantic speedup
is attributed to removing acquisition hashes.

## Production bridge

A separate executable has no SHA A/B switch; File Context's production policy
unconditionally skips content digests for fresh PUBLISH. It was compared with
the frozen current V2 used by the preceding ETW capture.
Two warmup pairs, 12 measured alternating pairs:

- Full PUBLISH medians: **1330.90 → 1272.90 ms**.
- Paired gain median: **50.80 ms**; mean 54.5492 ms.
- Bootstrap mean 95% CI: **[43.1992, 66.0975] ms**; faster **12/12 pairs**.
- Peak WS medians: 2573512704 / 2573549568 bytes; no material growth.

The bridge and primary series are separate; do not add their savings. This is
roughly 1.27–1.31 seconds in these series, not subsecond PUBLISH.

## Validation

All 44 primary, 28 bridge and 2 diagnostic full-PUBLISH calls preserved:

- compiled.bin: `a49506c8df1a999d68624f3a3fd0c317516015b7375deb992ced99736b75522d`
- runtime.bin: `a0281b3bb48103a7cd339cccd072d24f3524db9932bc67b58a714906996b5ee6`

Hashes are checked by the external Python harness after each timed invocation.
The isolated fixture path stays `build/publish-subsecond-01/fixture/project.json`.

Frontend passed with SHA reference, no-SHA A/B and production no-switch policies;
all 22 retained semantic contract fixtures passed. SourceSave regression passed,
including ordinary hashed proof/lineage behavior. New checks cover explicit
unhashed snapshot bytes, equal/replaced re-adoption and rejection of aggregate
proofs for unhashed contexts. Both runtime-range variants passed compiled/runtime suite.

The first standalone production frontend test used different Source feature macros
than its reused objects and incorrectly expected compact-token behavior. Its build
configuration was corrected to the exact production Source defaults and the suite
passed. No Parser code was changed to satisfy that assertion.

## Reproduction and provenance

Root build: `build/publish-no-sha256-01`, Release + IPO, /Zi and /DEBUG:FULL,
`SERVER_ENGINE_PUBLISH_NO_SHA256_AB=ON` only for the reference/candidate experiment.
Default OFF in production; there is no production environment override.

Standalone targets in `experiments/publish-lifecycle-01` reuse objects from this
complete root build. ABI changed when acquisition policy fields were added;
objects from older builds must **not** be reused. Production target recompiles
File Context without the A/B macro. All other sources that inspect the new ABI
come from the new complete build.

```powershell
cmake -S . -B build/publish-no-sha256-01 -G "Visual Studio 18 2026" -A x64 -DSERVER_ENGINE_BUILD_BENCHMARKS=ON -DSERVER_ENGINE_ENABLE_IPO=ON -DSERVER_ENGINE_PUBLISH_NO_SHA256_AB=ON "-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /Zi" "-DCMAKE_EXE_LINKER_FLAGS_RELEASE=/DEBUG:FULL /OPT:REF /OPT:ICF /INCREMENTAL:NO"
cmake --build build/publish-no-sha256-01 --config Release --target ServerEngineV4PublishBenchmark ServerEngineV4FrontendTests ServerEngineV4SourceSaveTests -- /m:4
python experiments/header-v2-state-lazy-01/run.py build/publish-no-sha256-01/Release/ServerEngineV4PublishBenchmark.exe build/publish-subsecond-01/fixture/project.json build/publish-no-sha256-01/new-results --pairs 20 --control-env CW_PUBLISH_SHA256_REFERENCE
```

Raw data: `build/publish-no-sha256-01/results-20261023/`,
`production-bridge-20261023/`, `build/publish-lifecycle-01/hash-audit/`.

Other two investigations were measured separately with no-SHA common to both arms:
see `RUNTIME_API_RANGES_01.md` and `CONSTRUCTION_LIFETIME_01.md`. Neither is enabled
in production by this change.
