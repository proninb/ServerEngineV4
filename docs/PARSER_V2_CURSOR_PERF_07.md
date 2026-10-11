# PARSER-V2-CURSOR-PERF-07

## Scope

Benchmark-only cursor/preprocessor instrumentation. Requires PERF-05 and DETAIL-06.
The build target `ServerEngineV4ParserV2ProjectBenchmark` already defines
`CW_HEADER_V2_SEMANTIC_PROFILE`; production and regular tests do not define it.
No change to grammar, G, compiled.bin, SHM, or semantics of preprocessing.

## Measurements

The `cursor-detail` mode preserves the original PERF-02, PERF-05 and PERF-06
CSV columns and appends, for each of `decode`, `cursor_advance`, `seek_next`,
`macro_expand`, `directive`: `calls`, `samples`, `sample_ms`, `estimated_ms`.
Timers fire on every 128th call; counts are exact, estimates extrapolated.

- `decode`: time inside `semantic_cursor_v2::decode_current` (also called at start).
- `cursor_advance`: time of the native cursor advance, which includes decode.
- `seek_next`: time of filtering, includes, macro expansion and directive processing.
- `macro_expand`: time spent in `preprocessing.expand` for semantic identifiers.
- `directive`: time inside `consume_directive` reached through `seek_next`.
- `raw_tokens_seen`: iterations of `seek_next` that reach a non-finished raw token;
  directive consumption may advance past other raw tokens internally.
- `identifiers_seen`: active identifiers for which macro expansion is attempted.
- `identifiers_unchanged`, `identifiers_replaced`, `identifiers_elided`:
  expansion outcome after a successful expansion; their sum equals `identifiers_seen`
  on successful complete parses.
- `identifiers_with_definitions`: number of active identifier encounters while
  at least one macro definition exists (NOT number of changed identifiers).
- `inactive_tokens_skipped`: inactive tokens skipped inside `seek_next`.

All timings are non-exclusive and non-additive; seek-next includes expansion and
some cursor operations, cursor-advance includes decode. Sampling periodicity
can bias estimates. This profile adds overhead and is not a production timing.

## Run

```
cmake --build build --config Release --target ServerEngineV4FrontendTests ServerEngineV4ParserV2ProjectBenchmark
ctest --test-dir build -C Release --output-on-failure
.\build\Release\ServerEngineV4ParserV2ProjectBenchmark.exe compare .\build\unitproxl-transfer-check\project.json
.\benchmarks\run_parser_v2_unitproxl.ps1 -ProjectPath .\build\unitproxl-transfer-check\project.json -Mode cursor-detail -Runs 7 -Warmup 2 -ResultPath build\parser-v2-cursor-perf-07.csv -CompareOld
```

Keep Header Graph semantic parity and never remove macro support to improve
benchmark numbers. Changes of algorithm belong in a later optimized slice.
