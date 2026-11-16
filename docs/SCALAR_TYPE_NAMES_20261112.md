# Fixed-width and shorthand scalar names — 2026-11-12

Implemented locally; no commit/push/deployment. Names below are parser
conveniences resolving to intrinsic scalar types. An explicit Header type
declaration takes precedence, including nominal using declarations and template
type parameters. They do not implicitly create new nominal identities.

| Spellings | Canonical storage |
|---|---|
| int8_t, int8, i8, sbyte | signed char |
| uint8_t, uint8, u8, byte, uchar | unsigned char |
| int16_t, int16, i16 | short |
| uint16_t, uint16, u16, ushort | unsigned short |
| int32_t, int32, i32 | int |
| uint32_t, uint32, u32, uint | unsigned int |
| int64_t, int64, i64, intmax_t, longlong | long long |
| uint64_t, uint64, u64, uintmax_t, ulonglong | unsigned long long |
| ulong | unsigned long |

Widths are 8/16/32/64 bits on the supported Windows x64 ABI. Per the explicit
user choice, ulong follows C++ unsigned long: **32 bits on Windows x64**, not
64. On another ABI it follows that ABI's unsigned long width. The fixed-width
spellings retain their stated storage widths across the existing supported ABI
family; no pointer-sized or fast-integer aliases were guessed.

Both Header V2 and Source V2 support unclaimed `std::int8_t` through
`std::uint64_t`, plus `std::intmax_t`/`std::uintmax_t`. This is direct name
recognition, not standard-library header importing. Existing namespace/type
declarations shadow these conveniences. `std::byte` is not mapped to unsigned
char: it has different C++ enum semantics and is outside this shortcut set.

Global spellings also work in the legacy parser. The implementation shares one
mapping table without changing lexer keywords or prepopulating G with aliases.
No extra named-type lookup is performed for ordinary nonmatching type spellings.
Unknown widths such as uint128_t fail explicitly.

```cpp
struct Values { uint8_t flags; int16_t value; uint64_t counter; ulong mask; };
using PLC_INT = uint16_t; // distinct nominal type, with uint16_t storage
```

`Box<uint8_t>` and `Box<unsigned char>` reuse the same specialization.
`Box<PLC_INT>` remains distinct from `Box<uint16_t>`. Native representation,
initialization range checks, persistence and links use existing intrinsic logic.
No binary-format change is needed for the shortcuts alone; explicit nominal
using declarations retain the previously added v28 format requirement.

## Validation

- Existing frontend_regressions and compiled_project suites PASS.
- Dedicated Header cases cover 22 names through both legacy/V2 parsing, their
  exact intrinsic type_refs, arrays, std qualification and explicit shadowing.
- Real TCP PUBLISH/LOAD and fresh-process LOAD check sizes, signed minima and
  unsigned maxima for all four fixed widths; byte/uint/ulong widths; canonical
  template reuse; nominal using integration; and same-storage alias references.
- Overflow for uint8_t/int8_t/ulong, unknown uint128_t and the unsupported
  std::byte shortcut reject.
- Legacy REBUILD passes using global spellings. Semantic-changing BUILD remains
  blocked by the existing RUNTIME-BIN-02 restriction; it is not claimed as PASS.
- Updated template integration and a separate full UnitProXL PUBLISH/LOAD run
  passed artifact/native-SHM parity; this is not a performance A/B.

Evidence/harnesses:

- tests/scalar_type_names_regressions.inc
- build/scalar-type-names-tests-final.log
- experiments/header-templates-01/scalar_type_names_e2e.py
- build/scalar-type-names-e2e-final/summary.json
- build/scalar-type-names-unit/summary.json

The first TCP harness expected semantic-changing BUILD to succeed and hit the
existing deferred-operation diagnostic. Its raw log is retained. Later runs record
that restriction separately from grammar validation. Previous Task, template and
nominal-using work was preserved. PGO and heap optimization settings are unchanged.
