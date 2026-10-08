# LEX-SYMBOL-GROWTH-04 — geometric symbol buffer growth

Checkpoint: `work/parser-split-v1` at `365b5d9e15546e00cdea06bb72354ab5eb10af5c`.

## Cause

`lexical_symbol_stream_v2::record_identifier()` previously called
`reserve(size() + 1)` on every identifier for both `symbol_records` and
`identifier_symbols`. When size equals capacity, an explicit `reserve`
request of exactly `size + 1` can force a reallocation and copy all existing
elements. With many identifiers in a file this makes construction expensive
and potentially quadratic.

## Change

`reserve_for_append()` grows each vector capacity geometrically (starting at
8 and doubling, clamped to both `uint32_t` local-ID and `vector::max_size`
limits). The fast path performs no allocation when `size < capacity`.
Both buffers are reserved before a new record and its ID are published to
the hash table. Hashing, deduplication, local symbol IDs, token order,
scanning, format and Graph semantics are unchanged.

The hash index remains controlled by the existing `ensure_index_capacity()`;
its allocation and rehash logic are unchanged. An unsuccessful reservation
can change only buffer capacities/index capacity, not visible identifier
records or hash entries.

## Validation

Run from repository root in Windows PowerShell:

```powershell
cmake --build build --config Release --target ServerEngineV4FrontendTests ServerEngineV4ParserV2ProjectBenchmark
ctest --test-dir build -C Release --output-on-failure
.\build\Release\ServerEngineV4ParserV2ProjectBenchmark.exe compare .\build\unitproxl-transfer-check\project.json
.\benchmarks\run_parser_v2_unitproxl.ps1 -ProjectPath .\build\unitproxl-transfer-check\project.json -Mode profile -Runs 7 -Warmup 2 -ResultPath build\parser-v2-growth04-profile.csv -CompareOld
```

Compare against the pre-change `build/parser-v2-perf-03.csv` using median
`lexical_wall_ms`, `lexer_task_elapsed_sum_ms`, `physical_prepare_ms`, and
`measured_sum_ms`. These profile timings include optional instrumentation;
for final speedups also compare the unprofiled `-Mode v2` runs using fresh
output filenames. Require `HEADER_GRAPH_PARITY_PASS rows=801988` for UnitProXL.

MSVC Release and UnitProXL measurements must be run on Windows; a Python
patch preflight is not a substitute for the C++ build and tests.
