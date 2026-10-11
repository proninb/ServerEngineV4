# PARSER-V2-PERF-03 — Physical Preparation telemetry

Status: opt-in instrumentation; no algorithmic optimization or runtime/Graph changes.

## Contract

`prepare_header_include_closure_v2(..., failure, profile = nullptr)` optionally
collects timings. The default `nullptr` path performs no clock reads and allocates
no per-lane telemetry array. Per-task clock reads are made only in explicit profile
mode. The production call sites use the unchanged 5-argument form.

Measured wall times in milliseconds (per entire closure):

- `workers_start_ms`: startup of fixed execution lanes.
- `materialize_wall_ms`: acquisition job preparation + worker execution + apply.
- `acquisition_prepare_ms`: prepare/reserve acquisition jobs and content checks.
- `acquisition_read_wall_ms`: `workers.run` during file acquisition, including wait.
- `acquisition_apply_ms`: owner-thread publication of acquired file content.
- `lexical_wall_ms`: `workers.run` for Lexer + numeric literal preparation.
- `include_scan_ms`: decoded-token include scan and path resolution.
- `frontier_sort_ms`: sorting and swapping next-frontier file IDs.
- `finalize_ms`: completion of include state and prepared file view publication.
- `total_ms`: from entry into physical closure builder run to return from finalize.

`read_task_elapsed_sum_ms`, `lexer_task_elapsed_sum_ms`, and
`literal_task_elapsed_sum_ms` sum **elapsed durations of individual tasks across
workers**, not CPU time or serial wall time. These totals may exceed the
corresponding wall times due to concurrency; **never add them to wall phases**.
`other_wall_ms` in the profile CSV is computed as total minus the disjoint outer
phase timers (startup, materialize, lexical, scan, sort, finalize), so it includes
root registration, validation, loop book-keeping and profiling overhead.

Counters: number of processed frontiers, peak frontier width, acquisition jobs,
Lexer-file tasks, finalized ready files, and discovered include directives.

## Benchmark

```powershell
cd C:\Boris\ServerEngineV4
cmake --build build --config Release --target ServerEngineV4ParserV2ProjectBenchmark
.\build\Release\ServerEngineV4ParserV2ProjectBenchmark.exe profile .\build\unitproxl-transfer-check\project.json 7 2
.\benchmarks\run_parser_v2_unitproxl.ps1 -ProjectPath .\build\unitproxl-transfer-check\project.json -Mode profile -Runs 7 -Warmup 2 -ResultPath build/parser-v2-perf-03.csv
```

`v2`, `old` and `compare` modes retain the original PERF-02 output schema.
`profile` prints the existing columns followed by detailed PERF-03 columns.
The PowerShell runner still requires a **new output path**, never overwrites an
existing measurement file, and can be run with `-CompareOld` to check parity.

Use separate `v2` (unprofiled) and `profile` (instrumented) runs to quantify
instrumentation overhead. Do not optimize or change parser grammar based on a
single run. Verify OLD/V2 Graph parity separately before any future algorithmic
change. The semantic projection covers Header Graph rows, not every runtime
artifact or persistence concern.
