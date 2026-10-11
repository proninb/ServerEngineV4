# HEADER-MODEL-V2-02B1 — effective-token experiment

Status: opt-in A/B research slice; **not** a completed general Header Model.
Depends on local HEADER-MODEL-V2-01 and 02A installers, PUBLISH Header V2.

## Actual implementation

1. `semantic_preprocessor_v2::materialize_effective()` consumes the current
   active macro/include stream once into `vector<prepared_token>` and enables
   replay through `current/at/advance/finished`. This preserves effective
   file_id/offset and canonical string_id. The original preprocess path stays
   available when the flag is OFF.
2. `header_effective_model_v2` discovers independent struct/class/union bodies
   from the effective stream, dispatches record body scans to `execution_lanes`,
   and produces compact ranges for simple fields. It can recognize constructor
   boundaries but does not yet parse constructor operands into semantic operations.
3. `header_parser_v2::try_effective_field` skips the second syntactic `parse_type`
   + `parse_member_declarator` for **simple, unqualified by-value fields without
   initializer/arrays/references**. It resolves named and intrinsic types
   against G and appends `member_record`/`construction_value` directly to the
   original record's semantic working vectors, including pending resolution.
   Existing `G.define_resolved_record` remains the only final Graph commit.
4. Reference fields, array fields, constructors and methods retain their old
   Header V2 semantic paths, preserving reference binding/defaults. This is
   explicitly NOT yet direct commit for every record/member/constructor.

## Safety boundaries

- No other Graph or AST. Models are temporary, read-only for G, ordered semantically.
- No Graph mutation by worker lanes.
- Full preprocessing occurs **before** parser validation when ON, so on invalid
  input the precedence of preprocessor-vs-parser diagnostics may change. Do not
  publish until negative/error regressions have been explicitly validated.
- Canonical string interning order may differ when preprocessing runs ahead;
  **BYTE PARITY is NOT presumed**. If parity fails, do NOT adopt the model; report
  first SHA and metrics for design correction. Never 'fix' a SHA by modifying
  the expected value.
- Bench must count nonzero effective records, fields, and committed fields to
  show useful work. If any are zero, the experiment did not cover that path.

## CMake switches

`SERVER_ENGINE_HEADER_MODEL_V2=ON` (existing)
`SERVER_ENGINE_HEADER_MODEL_EFFECTIVE_V2=ON` (new)
`SERVER_ENGINE_SOURCE_PARSER_V2=ON` (existing)
`SERVER_ENGINE_SOURCE_MODEL_V2=ON` and
`SERVER_ENGINE_SOURCE_MODEL_DIRECT_V2=ON` (existing Source baseline).

## Metrics

`header_v2_effective_ms`: effective preprocessing and buffer materialization.
`header_v2_effective_model_ms`: boundary discovery + parallel record parsing.
`header_v2_effective_records`: number of matched complete records.
`header_v2_effective_fields`: modeled simple field declarations, including
 references/arrays that intentionally remain on old semantic path.
`header_v2_effective_constructors`: recognized constructor starts.
`header_v2_effective_committed`: fields actually routed through the direct
 semantic helper.

`header_ms` already encompasses effective preprocessing, model build and
validation in this slice; **do not sum** those substage durations again.

## Acceptance criteria

1. Release MSVC build and CTest pass.
2. UnitProXL SHA256 exactly `90ED1B05A7E1D098E9982EBC09BE8CBB4D95AB974578E91F5570C3D87AA03C10`.
3. `header_v2_effective_records>0` and `header_v2_effective_committed>0`.
4. Median comparison at least 7 paired runs (prefer A/B with equal flags
   except `HEADER_MODEL_EFFECTIVE_V2`).
5. Negative tests for duplicate fields, missing base, nested constructors,
   included declarations, macro replacement, invalid syntax, and stable
   compiled.bin identity mappings.
