# HEADER-V2-CURSOR-OPT-17 â€” redundant hidden-token clears

Baseline: accepted PUBLISH-PHYSICAL-01 commit `7478593`.

## Scope

On every successful semantic token transition the old implementation cleared
`prepared_token current_value` before decoding the next token. It also cleared
the semantic preprocessor's published token before `seek_next()`, and cleared
it again on an elided macro identifier.

Candidate removes only these three redundant clears:

- `semantic_cursor_v2::advance()` sets `current_valid=false`, increments the
  logical token offset and invokes the **unchanged** checked decoder.
- `semantic_preprocessor_v2::seek_next()` invalidates the previous token, then
  assigns `current_value=*raw` before publishing it. It does not clear the
  hidden previous payload.
- Elided identifiers leave the semantic token invalid until the next valid
  token is published or EOF is reached.

## Invariants

- `current()` returns `nullptr` and `at(kind)` is false whenever
  `current_valid == false`; no stale token is observable via public APIs.
- `semantic_cursor_v2::decode_current()`, checked delta/length, identifier
  resolution, literal count/validation, `finished()`, and error status are
  unchanged.
- `semantic_preprocessor_v2::fail()` and `start()` still explicitly reset
  their state; macro expansion, directives, conditionals and includes are
  unchanged.
- No new allocations, caches, token format, Graph projection or ABI changes.
- The optimization can be neutral if the compiler already removes these
  stores. Only an end-to-end paired PUBLISH A/B can justify accepting it.

## Gates

1. Release compile: production server, frontend regression tests, PUBLISH
   benchmark.
2. Native `frontend_regressions`, including EOF, malformed token and
   define/undefine/elided-identifier cases.
3. Dedicated NEW fixture with cold baseline audit and exact compiled.bin
   SHA-256 parity after every PUBLISH (21 paired AB runs).
4. Separate final baseline/candidate runtime.bin SHA-256 equality check.
5. Compare medians and paired wins; never auto-commit or push.

If the candidate does not improve total PUBLISH, reject. Keep accepted
`7478593` unchanged.