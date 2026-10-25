# PUBLISH lifecycle experiments

Build the complete `build/publish-no-sha256-01` root benchmark and frontend objects
first. Acquisition policy changed the ABI; earlier object directories are invalid.
Then configure/build this directory with MSVC Release+IPO.

Targets:

- `PublishHashAudit01`: diagnostic SHA calls/bytes; never use its timings.
- `RuntimeRanges01`: per-executor memo of successful immutable API range validation.
- `ConstructionLifetime01`: optional context reset before SHM materialization.
- `LifecycleTests01`: compiled/runtime suite with both range policies.
- `PublishNoShaProduction01`: accepted no-SHA policy without A/B switch.
- `PublishNoShaFrontend01`: production-policy frontend suite, matching the reused
  objects' Source feature macros.

All A/B targets default SHA reference to OFF when its selector is unset. Runtime
and lifetime series therefore compare against no-SHA, one independent change each.

Selectors: `CW_PUBLISH_SHA256_REFERENCE`, `CW_RUNTIME_RANGES_REFERENCE`,
`CW_EARLY_RELEASE_REFERENCE` (`1` reference / `0` candidate).
The last two candidates remain experiment-only and are not enabled in root builds.

Reports: [no SHA](../../docs/PUBLISH_NO_SHA256_01.md),
[range memo](../../docs/RUNTIME_API_RANGES_01.md),
[lifetime](../../docs/CONSTRUCTION_LIFETIME_01.md).

```powershell
cmake -S experiments/publish-lifecycle-01 -B build/publish-lifecycle-01 -G "Visual Studio 18 2026" -A x64
cmake --build build/publish-lifecycle-01 --config Release -- /m:3
ctest --test-dir build/publish-lifecycle-01 -C Release --output-on-failure
```

For timing use `experiments/header-v2-state-lazy-01/run.py` with the appropriate
selector and a new output directory. Its CSV eager/lazy labels mean reference/
candidate regardless of experiment. Both golden files are checked outside timing.
