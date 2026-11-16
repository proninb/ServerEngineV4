# Header V2: string defaults and namespace-scope static objects

Implemented locally, 2026-11-11. No commit, push or deployment.

PUBLISH Header V2 now accepts:

```cpp
typedef int TYPE;
static TYPE VAR = 7;
struct C {
    const char* AAA = "aaa";
    char BBB[4] = "aaa";
    int& R = VAR;
};
```

AAA receives a relocated pointer into Project-owned literal storage. Literal
bytes including NUL are persisted as internal static char-array objects using
existing G object bindings and initialization records. No parser-process address
is persisted; no binary-format version is introduced. The public pointer is
const-qualified; the backing mapping is not protected read-only by the OS.

BBB receives per-object constructor defaults; remaining elements are zeroed.
Explicit constructor writes take precedence, including constructors preceding
the declaration. Adjacent ordinary literals, standard escapes, octal and
byte-sized hexadecimal escapes are handled.

Header static objects have separate identities per semantic Header root,
including inside namespaces. Reference defaults check the complete record member
scope before enclosing static objects, preserving member shadowing.

## Validation

- Full frontend_regressions PASS, including 22 new acceptance/rejection cases,
  constructor precedence, member shadowing and a separate two-root static test.
- Existing compiled_project PASS (graph, persistence and SHM regressions).
- Real TCP PUBLISH, RUN/FREEZE, UNLOAD, LOAD and fresh-process LOAD PASS.
  External native-memory inspection verified two objects, independent mutable
  arrays, padding, inherited defaults, embedded NUL, byte 0xff and relocated
  pointers to complete literals. Static references retained 7 and 9 across roots.
  Artifact hashes remained unchanged across LOAD; TCP survived UNLOAD.
- TCP negative fixtures rejected short arrays, mutable pointer literals, invalid
  escapes and conflicting static declarations; the server remained usable.

Evidence: build/header-literals-final-tests.log and
build/header-literals-static-e2e-02/summary.json.
Reproduction: experiments/header-literals-static-01/tcp_e2e.py with a fresh results
directory. Server SHA-256:
`a3c847de9428965422c11cabf8e58e4bb45c6f454a45a39a8b8293c22665bec3`.

The first harness used unsupported TCP GET_VALUE array-index queries; its failed
attempt remains in build/header-literals-static-e2e. The corrected test inspects
native pack8 memory externally without extending the TCP query API.

## Limits

This extends Header V2 for PUBLISH; legacy BUILD string parsing was not extended.
Class static data members, wide/raw literals, inferred array bounds and global
static pointer-to-literal initializers remain unsupported by this change. The
global/namespace static path supports scalar constants, zero initialization and
bounded char-array strings. Mutable char-pointer literals and arrays lacking room
for NUL are rejected, consistent with strict modern C++.

Previous Task runtime changes were preserved. PGO, heap optimization and release
adoption settings were not changed.
