# PARSER-V2-CURSOR-AB-DIRECTIVE-10

This slice answers two questions without changing Parser V2 semantics.

## A/B control

`ServerEngineV4ParserV2ProjectBenchmarkReference` is built from the same
sources and compiler settings as `ServerEngineV4ParserV2ProjectBenchmark`.
It alone defines `CW_PARSER_V2_DECODE09_REFERENCE`, switching the
`semantic_cursor_v2` decoder back to its *pre-DECODE-09* form at compile time.
The normal project executable, tests and production continue to use DECODE-09.

Run the PowerShell crossover script. By default it builds both targets,
optionally validates each against OLD (`-CompareOld`), warms up with 2 pairs
and performs 7 paired trials. Trial order alternates A/B and B/A. Each
trial is a new process; both receive the same project.json. The runner
checks identical CSV schemas and structural result counts. It refuses to
overwrite an existing CSV and writes only after all measurements pass.
A/B comparison should focus on paired **semantic_parse_graph** differences,
not on unrelated variability in acquisition or manifest preparation.

## Directive grouping

`cursor-detail` additionally reports exact *dispatched* directive counts:
`perf10_include`, `perf10_pragma`, `perf10_define_undef`,
`perf10_conditional`, and `perf10_other`. Inactive directives still count if
consumed by the preprocessor; counts describe dispatch, not semantic effects.
These counters are present only when `CW_HEADER_V2_SEMANTIC_PROFILE` is
compiled and a non-null profile is passed. The ordinary v2 CSV is unchanged.

## Commands

```powershell
cmake --build build --config Release --parallel 1 --target ServerEngineV4FrontendTests ServerEngineV4ParserV2ProjectBenchmark ServerEngineV4ParserV2ProjectBenchmarkReference
ctest --test-dir build -C Release --output-on-failure
.\benchmarks\run_parser_v2_decode09_paired_ab.ps1 -ProjectPath .\build\unitproxl-transfer-check\project.json -Pairs 7 -WarmupPairs 2 -ResultPath build\parser-v2-decode09-paired-ab.csv -CompareOld
.\benchmarks\run_parser_v2_unitproxl.ps1 -ProjectPath .\build\unitproxl-transfer-check\project.json -Mode cursor-detail -Runs 7 -Warmup 2 -ResultPath build\parser-v2-directive-10.csv
```

No rewrites of compiled.bin, Graph G, WHO/WHERE, SHM or Runtime.
