# PARSER-V2-SEMANTIC-PERF-05

Baseline: `cc24108de8bbee53552252afe15d39c1608e321f` on `work/parser-split-v1`.

## Scope

Instrument only the opt-in `ServerEngineV4ParserV2ProjectBenchmark` target with
`CW_HEADER_V2_SEMANTIC_PROFILE`. The production and test targets compile without
this definition; the Header Parser V2 production class layout and hot paths are
unchanged. The Header grammar, G, compiled.bin and SHM are untouched.

Run: `ServerEngineV4ParserV2ProjectBenchmark.exe semantic-profile <project.json> 7 2`.

The first 16 CSV fields are the original PERF-02 fields. The `semantic-profile`
mode appends:

- `perf05_type_calls` and `perf05_member_calls`: exact invocation counts.
- `perf05_type_samples` and `perf05_member_samples`: every 128th invocation.
- `perf05_type_sample_ms` and `perf05_member_sample_ms`: wall time of the sampled
  invocations only, accumulated over the run.
- `perf05_type_estimated_ms` and `perf05_member_estimated_ms`: sampled-time
  extrapolations. These are **estimates**, not exact durations and may be biased
  by ordering or skew in record size/type complexity.
- `perf05_constructor_calls`, `perf05_constructor_ms`: exact measured managed
  constructor invocations and their wall time.
- `perf05_graph_define_calls`, `perf05_graph_defined_members`,
  `perf05_graph_define_ms`: exact successful record-definition count and summed
  wall time of `G.define_resolved_record` calls.

These metrics are **not all additive**: `parse_type` is nested inside higher
level record parsing. The full `header_semantic_parse_graph_ms` remains the
primary end-to-end wall metric.

Semantic-profile timing itself adds overhead. Compare `v2` with the prior
unprofiled benchmark and use `semantic-profile` ONLY to prioritize further
measurement. Maintain the independent `compare` oracle, which must report
`HEADER_GRAPH_PARITY_PASS rows=801988` for UnitProXL.

## Validation

```
cmake --build build --config Release --target ServerEngineV4FrontendTests ServerEngineV4ParserV2ProjectBenchmark
ctest --test-dir build -C Release --output-on-failure
.\build\Release\ServerEngineV4ParserV2ProjectBenchmark.exe compare .\build\unitproxl-transfer-check\project.json
.\benchmarks\run_parser_v2_unitproxl.ps1 -ProjectPath .\build\unitproxl-transfer-check\project.json -Mode semantic-profile -Runs 7 -Warmup 2 -ResultPath build\parser-v2-semantic-perf-05.csv -CompareOld
```

Never optimize Graph creation or remove semantic validation merely to improve
profile numbers. Use the results to select a separate algorithmic optimization.
