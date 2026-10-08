# FRONTEND-PHYSICAL-11 — Anchor replay of include directives

## Scope and invariant

Only the **physical** include-discovery scanner changes. The lexer already
records `lexical_directive_anchor{word_offset, source_base}` for every
preprocessor directive. Instead of decoding each ordinary lexical token again,
`scan_includes()` visits those anchors directly. An include ordinal still
counts **every physical `#include` occurrence**, including inactive/missing
candidates, and the same `resolve_candidate()` code executes unchanged.

`source_base` equals the *previous token start source offset* (zero for token
zero); it is NOT the directive's own source start. Calling
`decode_physical_token_v2(words, word_offset, source_base, is_first, ...)`
restores the directive's correct source position, including extended deltas.
The lexer-generated anchored stream is trusted to be valid. In the reference
variant the old sequential decoder remains available behind
`CW_PREPARED_INCLUDE_SCAN_REFERENCE`, target-private to the benchmark.

The production path never uses the reference flag. Parser V2, Graph G, WHO,
WHERE, source map, SHM and persisted artifacts are unchanged.

## Measurements

`profile` CSV appends `perf11_include_walk_ms` (the wall time in
`scan_includes()` outside `resolve_candidate()`), `perf11_candidate_ms` (whole
`resolve_candidate()`), and nested `perf11_filesystem_probe_ms`,
`perf11_file_resolve_ms`, and `perf11_ensure_file_ms`, plus their exact call
counts. `include_scan_ms` remains the outer wall timer. Nested fields MUST NOT
be added to form wall time: candidate includes its inner probe/resolve steps.
Profile timing adds overhead and MUST NOT be used to claim production speedups.

## Validation

Run Release and frontend tests first; `test_prepared_include_anchor_replay_11`
covers four ordered includes with repeated ready target, inactive missing
candidate, 70k whitespace delta, 300-character inactive identifier, and
`#pragma once`. Tests cover only normal lexer-generated streams, not corruption
of internal preprocessor anchors. `-CompareOld` independently checks semantic
projection through both compiled variants.

```powershell
cmake --build build --config Release --parallel 1 --target ServerEngineV4FrontendTests ServerEngineV4ParserV2ProjectBenchmark ServerEngineV4ParserV2IncludeScanReference
ctest --test-dir build -C Release --output-on-failure
.\benchmarks\run_parser_v2_include_anchor_ab.ps1 -ProjectPath .\build\unitproxl-transfer-check\project.json -Pairs 7 -WarmupPairs 2 -ResultPath build\parser-v2-include-11-ab.csv -CompareOld -SkipBuild
.\benchmarks\run_parser_v2_include_anchor_ab.ps1 -ProjectPath .\build\unitproxl-transfer-check\project.json -Pairs 7 -WarmupPairs 2 -Detailed -ResultPath build\parser-v2-include-11-profile-ab.csv -SkipBuild
```

The benchmark deliberately excludes Source/Assign, compiled.bin persistence,
and Runtime publication. Neither full production PUBLISH gains nor correctness
of sparse BUILD are claimed by this benchmark.
