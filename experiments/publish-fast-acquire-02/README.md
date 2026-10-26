# PUBLISH-FAST-ACQUIRE-02

Execution-mode correction: the initial reader series ran inside the default
tool sandbox. For ordinary PUBLISH performance use the later unsandboxed A/B/C
reference in `docs/PUBLISH_UNITPROXL_ABC_01.md`, about 3% reader improvement.
`abc.py` runs the frozen historical/current-reference/current-fast executables
on the exact original fixture; `token_probe.py` supports the separate caller-
controlled sandbox crossover. An argument/label alone does not change sandbox.

Production change: fresh PUBLISH selects byte-only acquisition. Windows uses
CreateFileW / GetFileSizeEx / ReadFile; FILE_SHARE_READ is retained, so active
writers are rejected. BUILD/REBUILD retain stable snapshot/digest acquisition.
The portable fallback keeps the existing snapshot checks. Binary formats and
durability policy are unchanged.

The private snapshot adds only a reference switch, artifact-root override and
post-timing SHM digest. There are no production environment switches. Reference
and fast modes use the same Release+IPO executable and the same immutable input.
Reference restores the pre-change acquisition call with metadata checks; SHA-256
and USN remain disabled in both PUBLISH arms. PGO is OFF.

```powershell
python experiments/publish-fast-acquire-02/prepare.py build/publish-fast-acquire-02-new
cmake -S build/publish-fast-acquire-02-new/source -B build/publish-fast-acquire-02-new/build -G "Visual Studio 18 2026" -A x64 -DBUILD_TESTING=ON -DSERVER_ENGINE_BUILD_BENCHMARKS=ON -DSERVER_ENGINE_ENABLE_IPO=ON
cmake --build build/publish-fast-acquire-02-new/build --config Release --target ServerEngineV4PublishBenchmark ServerEngineV4SourceSaveTests ServerEngineV4FrontendTests ServerEngineV4Tests --parallel 2
ctest --test-dir build/publish-fast-acquire-02-new/build -C Release -R "^(source_save|frontend_regressions|compiled_project)$" --output-on-failure
python experiments/publish-fast-acquire-02/test.py build/publish-fast-acquire-02-new
python experiments/publish-fast-acquire-02/run.py build/publish-fast-acquire-02-new
```

Requires retained common corpora and their manifest from
`build/publish-pipeline-01-compact/input-manifest.json`, plus the small fixtures
in that experiment's `cases/`. No timing runs overlap builds or tests. The
runner performs 15 alternating native pairs and 7 separate profile pairs on
full UnitProXL, retaining every observation. File hashes are checked after every
operation. Synthetic and full-corpus PUBLISH/audit/LOAD SHM parity is checked
outside the timing series. Input, binary and source manifests are checked again
at the end. Bootstrap intervals resample paired total-time differences (20,000
resamples, deterministic seed); positive gain means fast is faster.

Results live under the selected output directory: `summary.json`, `samples.json`,
`checks.json`, `parity.json`, `verified.json`, raw `timing-logs/`, source snapshot
and frozen executable. Main report: `docs/PUBLISH_FAST_ACQUIRE_02.md`.
