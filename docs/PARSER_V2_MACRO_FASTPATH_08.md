# PARSER-V2-MACRO-FASTPATH-08

## Scope

Accelerate zero-definition identifier expansion in `preprocessor::expand()`.
This is a direct C++ path used by Semantic Preprocessor V2. It does not change
Header Parser V2, Graph G, lexical tokens, compiled.bin, SHM, or the ABI.

## Contract

- `strings.contains(name)` runs **before** the fast path and remains authoritative.
- `definition_count == 0` is the guard; **not** `definitions.empty()` because
  `reset()` and `undefine()` can leave the allocated hash table intact.
- For a valid name with no active definitions, return `success` and
  `preprocessor_expansion_kind::identifier` with `identifier == name`.
- With any active definition, execute the **unchanged** existing expansion code,
  retaining recursive replacement, empty expansion and cycle handling.
- No new caches, copy, handles, state, or special bypass in Header Parser.

## Validation

1. Preflight guarded installer: `python <patch>.py --check`.
2. Full Release build and CTest.
3. UnitProXL OLD/V2 graph parity (`HEADER_GRAPH_PARITY_PASS`).
4. Compare 7-run warmup-2 `v2` (unprofiled) and `cursor-detail` runs with the
   previously measured baseline, using **new** result paths.

The regression test checks initial zero state, replacement, empty chain,
`undefine()` of the last definition, `reset()` with retained capacity, and
invalid `string_id`. Performance effect must be established by A/B measurements;
PERF-07 sampling timings are not a guaranteed speedup.
