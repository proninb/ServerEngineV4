# HEADER-V2-OPT-08 — Existing-first canonical symbol merge

Status: accepted as the baseline of **experimental Header Parser V2**. Full ServerEngineV4 PUBLISH, persisted BUILD-lineage and Source/Assign remain separate validation gates.

## Algorithm

Lexical preparation retains per-file unique symbols, their FNV-1a hashes, and compact words. During semantic resolution, check each local spelling against the existing global `string_table`, using its existing lexical hash and exact string equality (`find_prehashed`). Bind existing `string_id` values directly. Only missing symbols are sorted with the original total order `(hash, length, spelling, file_id, local_symbol_id)` and interned in that order (`intern_prehashed`). Preserve zero-hash normalization and the old full-spelling hash-collision semantics. No new global caches, semantic AST, extra source pass, or Graph WHO/WHERE or compiled.bin format changes.

## Windows Release / UnitProXL Header-only measurements

First uninstrumented 31-pair A/B: mean saved **37.826 ms** (25/31 wins). Second independent 31-pair A/B: mean saved **39.757 ms**, median paired saving **38.093 ms** (31/31 wins). Exact Graph, WHO/WHERE and Source Map cross-executable parity: 229,557,898 bytes, SHA-256 `06AE2653203F584DADCD112C7BA4E9ECA58EAF027E5013CD50AF22BCC781377F`.

Instrumented OPT-08 merge: median **28.377 ms** with sorting median **2.140 ms** and **50,199** pending new symbols (out of approximately 560,325 local symbols). These profile timings are separate from uninstrumented A/B and must not be added to Semantic wall times.

## Correctness

The native `ServerEngineV4FrontendTests` adversarial regression compares the production optimized merge against an independent original all-symbol sorting oracle. Coverage includes 1, 2, 5 and 7 files, repeated names within and across files, prepopulated global tables, a genuine pair of colliding 32-bit FNV-1a names, exact per-file local→global IDs and entire string-table slot order.

## Limitations

These are **Header-only V2** measurements. They do not establish full-project PUBLISH/REBUILD speedups or compatibility with persisted mmap BUILD-lineage snapshots. Check those separately before enabling the optimized frontend for production construction.
