# FRONTEND-PHYSICAL-12 — Positive include resolution reuse

## Scope

`prepared_include_builder_v2` owns a **single-use**, single-threaded memo
`(local candidate path, quoted/angled form) -> file_id`. Paths are native
`std::filesystem::path` values, not textual hash identities; collisions are
resolved by exact equality. Configuration and include directories remain fixed
for the duration of one preparation.

Only **confirmed** positive targets enter the memo, after normal ordered path
probes, `file_context::resolve()`, and `ensure_file()` have succeeded. A later
matching include in the *same physical include closure* may reuse the target
without another filesystem probe or file-ID resolution. This does not bypass
`ensure_file()`, which publishes the prepared record and schedules the target
for the next lexical frontier when needed. The memo is destroyed at the end
of `prepare_header_include_closure_v2()`, including failed preparations.

Relative quoted and angled headers remain separate lookup keys because their
search order differs. Absolute paths are represented as native local paths.
There is no negative caching: missing, invalid, and I/O candidates are
rechecked on every occurrence. No cache survives into a later BUILD/REBUILD,
so new physical snapshots recheck the filesystem. Intra-run path decisions
are deliberately stable once positively resolved; file-content acquisition
still verifies/loads the target in `materialize_frontier()`. The existing
physical snapshot consistency rules are unchanged; this change makes no
claim of atomic filesystem snapshots under concurrent external mutations.

This operation-local hint neither assigns `string_id` to speculative files nor
publishes semantic dependencies. No change to inactive-include semantics,
include ordinals, `Graph G`, WHO/WHERE, Source Map, runtime, compiled.bin,
or SHM.

## A/B and counters

`ServerEngineV4ParserV2IncludeResolveReference` has the same anchored scan
and decoder as the normal Parser V2 benchmark, but `CW_PREPARED_INCLUDE_RESOLUTION_REFERENCE`
disables only the positive lookup. Both builds expose the same profile schema:
`perf12_cache_lookups`, `perf12_cache_misses`, `perf12_positive_cache_hits`,
`perf12_positive_cache_entries`. `lookups == misses + hits`. Regression tests
cover shared-directory positive reuse, two repeated missing candidates, and
quoted vs angled precedence when local and include-directory files conflict.
Old PHYSICAL-11
`perf11_filesystem_probe_calls` and times reveal avoided filesystem work.

`run_parser_v2_include_resolution_ab.ps1` performs AB/BA paired runs, optionally
with old/v2 Graph semantic parity, and saves a **new** CSV without replacing
previous benchmark outputs. Profile clock measurements are nested and should
not be added to claim wall-time speedups.

## Release / UnitProXL gate

```powershell
cmake --build build --config Release --parallel 1 --target ServerEngineV4FrontendTests ServerEngineV4ParserV2ProjectBenchmark ServerEngineV4ParserV2IncludeResolveReference
ctest --test-dir build -C Release --output-on-failure
.\benchmarks\run_parser_v2_include_resolution_ab.ps1 -ProjectPath .\build\unitproxl-transfer-check\project.json -Pairs 7 -WarmupPairs 2 -ResultPath build\parser-v2-include-12-ab.csv -CompareOld -SkipBuild
.\benchmarks\run_parser_v2_include_resolution_ab.ps1 -ProjectPath .\build\unitproxl-transfer-check\project.json -Pairs 7 -WarmupPairs 2 -Detailed -ResultPath build\parser-v2-include-12-profile-ab.csv -SkipBuild
```

Semantic Source/Assign, persisted Graph writes, and Runtime publication are
outside the benchmark. No speedup is asserted until Windows Release AB/BA.
