# PUBLISH-PHYSICAL-01 — eliminate duplicate initial Header lexing

Baseline: `1acc3a6acbed6c89fcb6e3b37aa90adee47e3510`.

## Contract

- `PUBLISH`: old `lexical_generation` tokenizes Source roots only. Header roots
  remain registered and materialized using existing parallel acquisition; `prepare_header_include_closure_v2` owns
  Header lexing, includes and literals; `header_parser_v2` publishes to the
  same Graph used by Source parser.
- `REBUILD`: unchanged `all_roots` lexical behavior.
- `BUILD`: unchanged replacement path and policy.
- `compiled.bin`, `runtime.bin`, identifiers, Graph, Source Map, WHO/WHERE and
  persistence format are unchanged.
- Header-only projects have no old lexer work but still materialize their files.
- The scope flag is internal physical preparation policy, not a new grammar.

## Acceptance gates

1. Release build + `frontend_regressions` CTest (source-only, all-roots,
   Header-only, independent Header V2 publish).
2. A/B with `benchmarks/compare_publish.ps1` on a NEW isolated fixture;
   exact SHA-256 of persisted `compiled.bin` after each PUBLISH;
   baseline cold `audit` after every run.
3. Repeat on `mixed` fixture to cover Source+Header behavior.
4. Only accept as a performance optimization if measured end-to-end time improves.

PUBLISH benchmark fixtures and output are disposable; NEVER use a live project
for comparison because PUBLISH intentionally replaces final artifacts.
