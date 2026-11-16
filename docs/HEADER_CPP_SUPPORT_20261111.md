# Header C++ support audit — 2026-11-11

Historical pre-fix observations follow. Subsequent Header V2 support for string
defaults and global static objects is documented in
[HEADER_LITERALS_STATIC_FIX_20261111.md](HEADER_LITERALS_STATIC_FIX_20261111.md).

Result: partial C++ subset, not a general C++ header compiler or full type checker.
49 fixtures were parsed independently by LEGACY and PUBLISH_V2 and checked with
MSVC 19.51 syntax-only `/std:c++20 /permissive- /W4 /Zs /TP`. All observations
and compiler diagnostics are retained. MSVC acceptance is not ISO conformance
certification: notably it warns but accepts the zero-sized array extension.

No production parser changes. Existing local Task Runtime fixes were preserved.
Only test-only matrix/runner and this report were added for this request.

## Requested syntax in production Header V2

TYPE was concretized as `typedef int TYPE`; VAL as 7 (enum constants separately
tested); array extents as 4, macros, enum/constexpr names and expressions.
Results do not establish support for every user-defined TYPE or conversion.

| Form | PUBLISH V2 | Meaning / limitation |
|---|---|---|
| `static TYPE VAR = 7;` at header scope | Rejected | LEGACY accepts and records static object value 7. PUBLISH rejects before publishing that object. |
| `namespace N { static int VAR=7; }` | Rejected | Same backend mismatch. |
| `TYPE AA = 7;` | Accepted | Stored scalar default 7. |
| `TYPE BB{};` / `TYPE BB{7};` | Accepted | Zero / explicit scalar default. Semicolon is required. |
| `struct B {};` | Accepted | Empty record. |
| `struct C : A, B { ... };` | Accepted | Both base records retained; explicit public/private access also tested. |
| `int FUNC();` | Accepted | Declaration only; not an executable Runtime method or data field. |
| Ordinary method with parameters / const / noexcept | Accepted | Parameter token sequence is opaque, not fully type-checked. |
| Inline method body | Rejected | Declaration-only method subset. |
| `TYPE *X = nullptr;` / `int *X=0;` | Accepted | Null/zero pointer defaults. |
| `char* AAA="aaa";` | Rejected | Also rejected by strict MSVC because the literal is const. |
| `const char* AAA="aaa";` | Rejected | Valid C++ but string initializers are outside this Header initializer subset. |
| `char AAA[4]="aaa";` | Rejected | Same initializer restriction. |
| `const char* AAA;` | Accepted | Pointer type is supported; string initialization is the gap. |
| `TYPE ARR[4];` / `int ARR[2][3];` | Accepted | Positive literal dimensions retained in G. |
| `#define N 4` then `TYPE ARR[N];` | Rejected | This preprocessing subset rejects numeric object macro definitions; not a size-evaluation success. |
| Enum/constexpr bound or `[2+2]` | Rejected | Enum bound reaches V2 and requires a literal; constexpr header variable fails scope grammar; arithmetic bound fails at `]`. |
| `TYPE (&ARR_REF)[4];` | Accepted | Declaration-only reference field; no claim of default-constructible instantiated object. |
| `TYPE (&ARR_REF)[4] = ARR;` | Accepted | Canonical reference-to-array plus member binding retained. |
| Constructor `C():ARR_REF(ARR){}` | Accepted | Tested matching bounded-array reference binding. |
| `TYPE (&ARR_REF)[TYPE];` | Rejected | Also invalid C++ if the second TYPE is a type name rather than a value. |
| `TYPE &ARR[4];` | Rejected | Array of references is invalid. |
| `int (*ARR)[4]=nullptr;` / `int *ARR[4];` | Accepted | Pointer-to-array and array-of-pointers are distinct derived shapes. |
| Static data/member functions, static constexpr member | Rejected | Explicitly outside the instance ABI slice. |
| `int AA=1, BB=2;` | Rejected | Multiple data declarators unsupported. |
| `int AA=3+4;` | Rejected | General constant-expression evaluation unsupported. |
| `using TYPE=int;` / record typedef | Rejected | Typedef supports intrinsic scalar aliases, not arbitrary alias declarations. |
| Function pointer data member | Rejected | See legacy false acceptance below. |
| Nested record definition | Rejected | Tested `struct C { struct A {...}; A AA; };`. |
| `enum Values {VAL=7}; int AA=VAL;` in a member | Accepted | V2 retains default 7; legacy path rejects this fixture. |

## Confirmed compatibility / validation problems

1. **Header-scope static storage differs between PUBLISH and legacy.** The
   legacy graph contains one internal static object initialized to 7; V2 rejects
   the same valid header. This affects the published Header-static contract and
   cannot be inferred as supported from legacy-only static regression tests.
2. **Invalid C++ accepted:** `struct C { int X=nullptr; };` is accepted by both
   parsers and normalized to zero in G; MSVC rejects it. Likewise
   `int FUNC(UnknownType x);` is accepted because method parameters are opaque.
   Thus parse success is not proof of valid C++ source.
3. **Legacy function-pointer acceptance drops storage.** Legacy accepts
   `struct C { int (*FUNC)(int); };` but its semantic projection contains no
   FUNC data member. This is not usable function-pointer support or validated
   layout. V2 rejects it instead. No Runtime ABI support is inferred.

## Working combined subset

Both paths and MSVC accept:

```cpp
typedef int TYPE;
struct A { TYPE AA = 7; TYPE BB{}; };
struct B {};
struct C : A, B {
    int FUNC();
    char* AAA = nullptr;
    TYPE ARR[4];
    TYPE (&ARR_REF)[4] = ARR;
    TYPE* X = nullptr;
};
```

Verified G has both C bases, A::AA=7, A::BB=zero, C::AAA=null,
C::ARR=array[4] of int, C::ARR_REF=reference to array[4] with member binding,
and C::X=null. FUNC is absent from data slots, as intended for declarations.
This verifies semantic parsing, not method execution or full PUBLISH/LOAD/SHM
behavior for every matrix fixture.

## Reproduction and evidence

Build `ServerEngineV4FrontendTests`, then run in a NEW output directory:

```
ServerEngineV4FrontendTests.exe --header-support-matrix <new-output-directory>
python experiments/header-support-20261111/compare_compiler.py --results <directory> --build <cmake-build>
```

Test mode calls the actual parse_semantic_project entry with
publish_frontend_v2=true/false, separate file/string/identity/graph contexts, and
preserves semantic projections. Production PUBLISH selects true in
project_full_construction.cpp; no fallback to legacy occurs on V2 failure.

Evidence: `build/header-support-20261111-final/results.tsv`, `comparison.json`,
all 49 input headers and per-fixture MSVC logs. The initial matrix with the macro
in the combined example is retained separately; the final working example uses
literal dimensions to avoid that known preprocessing restriction.

Full existing frontend regression CTest: PASS (0.57 s), log
`build/header-support-20261111-final-regressions.log`. Matrix generation completion
does not mean all language features passed: rejects and false acceptances are
explicit observations. No grammar expansion, commit/push or deployment.
