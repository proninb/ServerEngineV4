# HEADER-MODEL-V2-02B2 — physical syntax preparation, streaming semantic commit

**Status:** opt-in experiment; do not adopt or commit until Release/MSVC tests
and UnitProXL byte parity pass. This patch is applied ON TOP OF 02B1; it
replaces that experiment's prepass/replay implementation (not a clean-room
standalone parser).

## Diagnosis from 02B1 A/B

Baseline and variant had identical sets of 187,939 strings, 23,333 types,
708,668 members, 137,584 objects, 367,144 links, and semantically equal
identity/member/constructor-default records, but the **first string slot
changed at 1817**. `pNOM_Params.pEnable` moved to slot 50200. The earlier
`materialize_effective()` registered included-file lexical symbols before
Header V2 synthesized constructor path strings, changing the string slot order
and byte representation of `compiled.bin`.

## What 02B2 changes

1. Completely removes `materialize_effective()`, `effective_replay`, the
   effective token buffer, and associated extra traversal. `start()/advance()`
   execute their original streaming preprocessing and interning sequence.
2. Independent worker lanes inspect **prepared physical lexical words** (before
   active preprocessing) and record *speculative*, simple `T member;` candidates
   in their physical `(file_id, source_offset)` coordinates. The lanes neither
   call `strings.intern()` nor mutate G or traverse active includes.
3. When the authoritative Header V2 parser is at one of these physical
   positions, it looks up the candidate. For named types and member names it
   reads canonical `string_id` from the **already interned current file**, and
   refuses the shortcut if either raw spelling is currently a defined macro.
4. The guarded fast path consumes the **live** preprocessor stream once,
   checking member/terminator coordinates, validates the type through G,
   appends the original `member_record` + construction slot and performs
   pending reference/nested callbacks. `G.define_resolved_record` remains the
   sole Graph definition path; methods, references, arrays, initializers,
   constructors and unsupported syntax fall back unchanged.
5. Existing `SERVER_ENGINE_HEADER_MODEL_EFFECTIVE_V2` opt-in flag, benchmark
   telemetry and CMake option are reused to avoid unrelated config churn.
   `header_v2_effective_ms=0` is expected (no full prepass). The physical
   model scan time is `header_v2_effective_model_ms`; the returned records/
   fields are PHYSICAL candidates, not guaranteed to be active declarations.

## Not yet implemented

- Full constructor models or direct handling of pointers/arrays/references.
- Cross-file statement spans or parsed syntax after macro expansion.
- Global front-end IR or runtime selection of A/B within one executable.

## Acceptance criteria

1. Do not reset the working tree or automatically change compiled artifacts.
2. Release build and CTest pass.
3. `ServerEngineV4PublishBenchmark` against UnitProXL generates exact
   `compiled.bin` SHA-256
   `90ED1B05A7E1D098E9982EBC09BE8CBB4D95AB974578E91F5570C3D87AA03C10`.
4. `header_v2_effective_fields>0` AND
   `header_v2_effective_committed>0` on the active path.
5. Ensure byte parity for actual active macro/no-macro, nested constructors,
   conditional includes, duplicate fields, and invalid sources.
6. Compare paired performance after warmup. Added physical scanner must beat
   the existing Header semantic path, not just reduce `parse_member()` calls.

## Safety note

The new fast candidate scanner is intentionally conservative and based on
three **consecutive physical** lexical tokens at record-body depth. At any
indication of macro involvement, source mismatch or unsupported syntax, use
original Parser V2. No candidate is allowed to publish without live stream
validation. The experiment has been tested here with an isolated C++20 fixture
and guarded installer dry runs, NOT Windows MSVC or UnitProXL.
