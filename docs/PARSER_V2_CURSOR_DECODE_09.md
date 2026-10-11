# PARSER-V2-CURSOR-DECODE-09

## Goal and scope

Fast decoding of *ordinary nonfirst lexical words*, while preserving compact
`lexical_token` format, extended delta/length layout, canonical identifier and
numeric occurrence resolution, and all bounds/diagnostic contracts.
No AST, no Graph persistence or WHO/WHERE changes, no SHM changes.

## Decoder

- `lexical_token::from_value` still validates token kind on every word.
- Common path: inline delta/length and a previously validated source offset.
  Validate `delta <= source_size - source_offset` and then
  `length <= source_size - source` without overflow.
- Fallback: first token, extended delta, extended length (including both),
  consuming extension words in the original delta-then-length order.
- Preserve checks that both occurrence streams are complete at EOF.
- Compile-profile-only exact counts (no per-token timing): `perf09_inline_tokens`,
  `perf09_fallback_tokens`, `perf09_extended_delta_tokens`,
  `perf09_extended_length_tokens` appended **only** to `cursor-detail` CSV.
  Normal `v2`, `profile`, `semantic-profile`, `semantic-detail` CSV unchanged.
- Sampled `perf07_decode_estimated_ms` remains available; its timer is
  embedded in benchmark-only code and does not exist in production.

## Validation / A-B

Release build and full frontend tests must pass. Verify old/new Header G parity.
Compare *unprofiled* `-Mode v2` on the same UnitProXL machine and project;
run an additional `cursor-detail` profile to inspect path fractions.
Use different result filenames; runner refuses overwrites.

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\ServerEngineV4ParserV2ProjectBenchmark.exe compare .\build\unitproxl-transfer-check\project.json
.\benchmarks\run_parser_v2_unitproxl.ps1 -ProjectPath .\build\unitproxl-transfer-check\project.json -Mode v2 -Runs 7 -Warmup 2 -ResultPath build\parser-v2-decode09.csv -CompareOld
.\benchmarks\run_parser_v2_unitproxl.ps1 -ProjectPath .\build\unitproxl-transfer-check\project.json -Mode cursor-detail -Runs 7 -Warmup 2 -ResultPath build\parser-v2-decode09-cursor.csv
```

Do not assume a speedup; ordinary-path branch may win or regress depending on
machine, cache locality and compiler. If A/B shows a regression, investigate
without changing the semantic contract.
