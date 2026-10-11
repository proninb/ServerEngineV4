# PARSER-V2-SEMANTIC-DETAIL-06

Depends on PARSER-V2-SEMANTIC-PERF-05 (compiled only in benchmark target).
No G, Runtime, SHM, source grammar, CMake, or production ABI modifications.

## Run

`ServerEngineV4ParserV2ProjectBenchmark.exe semantic-detail <project.json> 7 2`

or use `benchmarks/run_parser_v2_unitproxl.ps1 -Mode semantic-detail`.
The existing PERF-02 columns and PERF-05 columns are preserved as a prefix.
PERF-06 adds for each detail phase: `calls`, `samples`, `sample_ms`,
`estimated_ms` (sampling extrapolation, not exact wall time).

## Subphases

- `identity_lookup`: `find_type_identity` within `parse_type` (named only).
- `named_resolution`: `resolve_named_type` within `parse_type` (named only).
- `intrinsic_parse`: builtin grammar parsing within `parse_type`.
- `type_tail`: `parse_type_tail` (cv/pointer/reference resolution).
- `member_name`: record-local `names.insert` duplicate/index check.
- `member_initializer`: `parse_member_initializer`, including empty paths.
- `member_append`: append field and construction records to local vectors.
- `advance`: calls to `semantic_preprocessor_v2::advance` via
  `header_parser_v2::consume`. Counts only this call site, not internal
  preprocessor cursor steps during include replay/macro expansion.

All hot-path probes sample every 128th operation. Counts are exact for calls to
these particular call sites, not necessarily for all operations globally.
Timer overhead is included in the benchmark's semantic wall-time. Sampling
can be biased by repetitive field patterns. Nested timing makes the metrics
**non-additive**: advance is contained within parsing types, members, etc.
The sampled `member_initializer` time includes the empty-initializer fast path.
These measurements inform prioritization, not correctness or real CPU usage.

Compare measured `semantic-detail` against baseline unprofiled `v2` on
UnitProXL. The semantic Graph oracle must still report
`HEADER_GRAPH_PARITY_PASS rows=801988`.

This slice does not change the algorithm: keep Header V2 single pass,
Graph stable-WHERE, and no candidate compiled.bin.
