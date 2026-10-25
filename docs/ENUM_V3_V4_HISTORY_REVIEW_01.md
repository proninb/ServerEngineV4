# ENUM-V3-V4-HISTORY-REVIEW-01

2026-10-23. Read-only GitHub review requested before implementing const/enum support in V4.

## Repositories and revisions actually checked

The V3 repository is **proninb/ServerEngine**, not ServerEngineV3. README identifies it as Clean Architecture V3. Public GitHub repository inventory resolved the name; the guessed ServerEngineV3 URL returned repository not found.

- V3 main: `7bc31a158c1cb6e46e22e9059ed74116795e6548` (2026-09-21).
- V4 main: `bbc168b31453eb68dddb3fa749e1c5b6a788184d`.
- V4 working branch: `work/parser-split-v1`, `4abbd9c7d62a7e0a1a351f2e5b2fd0b99bdc0dd7`.
- All six published V4 branch heads and their reachable history (286 commits in the mirrored repository) were searched for parser/Graph enum support. The local working tree was also checked.

Separate read-only Git mirrors are under `build/enum-history-review/{serverengine.git,v4.git}`. The active checkout and its refs were not modified. The GitHub connector was unavailable; public GitHub metadata and git HTTPS reads were used instead.

## V3 already implements enum declarations, G representation and persistence

1. [Parser parse_enum](https://github.com/proninb/ServerEngine/blob/7bc31a158c1cb6e46e22e9059ed74116795e6548/server_engine/project/parser/source_parser.cpp#L2096): named enums, enum class/struct, explicit integral underlying types, permitted forward declarations, explicit signed decimal values, implicit numbering and canonical type identity. History attributes the parser addition to `2bc27c8`, Integrate V3 production Source frontend, 2026-09-10.
2. [Graph representation](https://github.com/proninb/ServerEngine/blob/7bc31a158c1cb6e46e22e9059ed74116795e6548/server_engine/project/graph/graph.hpp#L36): separate `graph_type_kind::enumeration`, enum underlying type, scoped/fixed-underlying flags, and a separate sixteen-byte `enum_value_record` holding bits, string_id name and integral category. Enumerators are not instance data members.
3. [Builder enum contract](https://github.com/proninb/ServerEngine/blob/7bc31a158c1cb6e46e22e9059ed74116795e6548/server_engine/project/builder/generation_builder.cpp#L2135): record/enum identity conflicts, scoped redeclaration conflicts, explicit underlying-type conflicts, and publication to canonical G. Both full and incremental Builder paths contain enum handling. `select_enum_underlying` supplies the range-based underlying selection logic.
4. [Persisted enum queries](https://github.com/proninb/ServerEngine/blob/7bc31a158c1cb6e46e22e9059ed74116795e6548/server_engine/project/persistence/compiled_image.cpp#L1322): `enum_value_count` and `enum_value` read separate persisted enum-value records through the containing enum type definition range.
5. [Parser test](https://github.com/proninb/ServerEngine/blob/7bc31a158c1cb6e46e22e9059ed74116795e6548/tests/foundation_tests.cpp#L915) and [Builder test](https://github.com/proninb/ServerEngine/blob/7bc31a158c1cb6e46e22e9059ed74116795e6548/tests/foundation_tests.cpp#L1850): scoped/fixed underlying metadata, explicit and implicit values, and distinct enumeration Graph entries.

## Limits of the earlier implementation

- Anonymous enums are explicitly unsupported.
- Explicit enumerator expressions are restricted to optionally signed decimal integer literals. This is not a general constant-expression evaluator.
- Parsing enum facts alone does not establish value representability in the eventual fixed underlying type. The parser test deliberately retains values before later Builder/ABI interpretation.
- The unsigned-64 implicit-numbering boundary needs a new regression before reuse: the parser stores a positive value above INT64_MAX as unsigned_long_long but sets its signed `implicit_value` counter to zero. Blindly reusing the following implicit-increment path would be unsafe.
- Full managed Runtime enum materialization is **not confirmed**. Its reader carries enum metadata, but `managed_runtime.cpp::scalar_layout` requires `graph.intrinsic(type, intrinsic)` and does not itself handle an enumeration kind. A complete current V4 SHM path must be verified independently.
- No historical test execution is claimed here; this is a source/history audit.

## V4 did not carry the feature forward

V4's current [Graph type kinds](https://github.com/proninb/ServerEngineV4/blob/4abbd9c7d62a7e0a1a351f2e5b2fd0b99bdc0dd7/server_engine/project/graph/graph.hpp#L211) are record and intrinsic_alias. Header Parser V2 has no enum dispatch or parse_enum. The lexer recognizes the C++ enum keyword, which is distinct from semantic support.

No implementation was found in the other published V4 branches (`main`, `work/header-v2-cursor-opt-17`, `work/ic-catalog`, `work/publish-v2-core-01`, `work/runtime-shm-v2`) or reachable history searches for parse_enum, enumerators, enum_type and Graph enumeration tags. This is a statement about the accessible published history, not deleted/unpublished commits or another private repository.

The observed UnitProXL_V2 failure at `enum U_TYPE_ENUM` is therefore consistent with the code. Earlier comments saying merely that enum is absent should distinguish **absent from V4** from **already implemented in V3**.

## Implementation direction retained

The user explicitly selected an independent enum type in G, including enum class and reference compatibility. Reuse V3's semantic contracts, separate enum-value representation, redeclaration rules and test ideas. Adapt publication to V4's direct G path; do not reintroduce source_facts, Local G or Generation Builder.

V4-specific work still required: Parser enum/value lookup, strict enum/reference identity, scoped and unscoped visibility, metadata persistence/LOAD, ABI/SHM execution and constructor/Source scalar consumption. Do not silently lower enums to intrinsic aliases or numeric preprocessor replacements. Existing artifacts without enums should retain their semantics; format compatibility and any new format version must be designed explicitly.

Const is a separate task. V4 already represents CV qualifiers; the observed ABB failure arises in scalar-initialization acceptance. Construction of final state must be distinguished from permission to mutate a const value in Runtime. This history review does not change either contract or claim those features are already implemented in V4.
