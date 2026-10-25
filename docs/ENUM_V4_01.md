# ENUM-V4-01 — named enum types in PUBLISH

Implemented in Header Parser V2 / Source Parser V2 and the compiled G → Runtime V2 path.

```cpp
enum Code { Failed=-1, Ready=0, Running=7 };
enum class Mode : unsigned char { Off, On=3, Next };
enum struct Status : unsigned short { Good=513 };

struct Device {
    Mode mode=Mode::On;
    Mode& input;
    int code=Running;
    Device():input(mode) {}
};

// Source
Device a;
Device b;
a.mode=Mode::Off;
a.input=b.mode;
```

## Semantic contract

- Enum is `graph_type_kind::enumeration`, with its own named `type_ref` / type WHO. It is never folded into an intrinsic alias by either V2 parser.
- Scoped values require qualification; unscoped values are visible in the enclosing scope. Namespace-qualified and nested enum names are supported.
- An explicit underlying type must be integral. Scoped enums default to `int`. Unscoped enums without an explicit base choose the first representation fitting all values: int, unsigned int, signed long long, unsigned long long.
- Forward declarations require a fixed underlying type (including implicit int for a scoped enum). Scopedness and the fixed base must agree on subsequent declarations.
- Identical definitions can be replayed from different Header roots into the same canonical G. Different definitions, duplicate enumerator names and conflicting unscoped names are rejected.
- Explicit optionally signed decimal integer literals and implicit increments are supported, including 64-bit range/overflow checks. General constexpr arithmetic, shifts, hex/suffixed literals and anonymous enums are outside this slice.
- An enum scalar initializer/assignment uses an enumerator of that same enum. Scoped values do not convert implicitly to an intrinsic scalar. Unscoped values may initialize compatible intrinsic scalars. Existing zero/default initialization remains supported.
- Enum references retain exact named type compatibility: enum-to-int and different-enum reference bindings are rejected.
- The existing V2 grammar still applies. This is not a full C++ expression evaluator or a new implementation of const.

## Representation and lifetime

The 20-byte type entry remains unchanged. Enum flags record scoped/fixed/body state and the underlying intrinsic type. Its tagged `members` range addresses enumerator names and numeric construction values in the existing dense columns. These entries are enum metadata, not instance fields. Endpoint resolution and Runtime field access distinguish the type kind.

A construction-only list of enum handles supports constant lookup without scanning every record type. During parsing, a numeric operand temporarily carries the enum WHO; validation erases that marker before G stores the final numeric construction value. No process pointer or parser lifetime is persisted.

Runtime V2 uses the underlying ABI size/alignment and scalar stores, while G retains the enum identity. Enum definitions are checked against the target ABI even when unused. Enumerator metadata does not become an object's fields or initialization instructions.

## Persistence compatibility

Enum-bearing compiled images use format 26. The section directory and physical record sizes remain unchanged. The reader also accepts existing format 25, but rejects enum tags disguised as format 25. A full PUBLISH without enums continues to emit format 25 to preserve existing artifacts byte for byte.

## Validation

The frontend regression suite covers enum / enum class / enum struct, opaque declarations, namespace/nested scopes, automatic values and 64-bit increments, constructor defaults, Source assignments, exact enum reference bindings, and invalid declarations/assignments.

The compiled-project suite independently constructs enum G metadata, round-trips compiled.bin through the validating reader, materializes Runtime V2, and checks actual underlying bytes and reference addresses. Existing frontend semantic contract fixtures remain enabled.

Scope: production PUBLISH and LOAD. The legacy REBUILD / incremental BUILD parser has not been extended to parse enum declarations in this change. No routing switch to a new Header pipeline, Local G or Generation Builder was introduced.

## Measured verification

- Original UnitProXL full PUBLISH still produces the existing golden compiled.bin SHA-256 `a49506c8df1a999d68624f3a3fd0c317516015b7375deb992ced99736b75522d` and runtime.bin SHA-256 `a0281b3bb48103a7cd339cccd072d24f3524db9932bc67b58a714906996b5ee6`. Digests are external test checks, not production hashing.
- The isolated enum fixture completes full PUBLISH and a separate LOAD, including enum-valued object/member defaults, nested zero overrides, enum arrays and a reference link.
- The prepared UnitProXL_V2 fixture passes its enum declarations and stops at `unity_pro_xl_base.h:75`, `type = U_TYPE_BOOL`: inherited constructor target fields are a pre-existing unsupported Header V2 category. Full UnitProXL_V2 PUBLISH is therefore not claimed.

Final checks: 22/22 tests passed in `build/enum-v4` with the unrelated communication executable excluded. That executable was blocked at its original output path (`LNK1104`); a clean independent build of the unchanged communication regression target passed 1/1 in `build/enum-v4-comm-check`. Enum persistence tests include the deep semantic audit, enum array constructor defaults and rejection of enum tags in format 25.
