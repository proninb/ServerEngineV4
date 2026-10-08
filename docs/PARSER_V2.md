# Parser V2

## Purpose

Parser V2 is a new frontend path. The existing `parser.cpp` remains the
production/correctness oracle until V2 reaches syntax/semantic parity.

## Layer contract

```text
PHYSICAL / LEXICAL
        |
        v
Source Preparation
    parallel read
    parallel lex
    direct include preparation
    file-local symbols
        |
        v
Deterministic Symbol Merge
    unique local spellings
    sort (hash, length, spelling)
    exact collision resolution
    dense string_id allocation
        |
        v
Semantic Input
    NO filesystem
    NO lexer
    NO spelling hashing/re-intern
    preprocessing state only
    include stack only
        |
        v
Header Parser / Source Parser
    grammar
    supported language semantics
    diagnostics
    resolved semantic values
        |
        v
Graph
    storage
    canonical indexes
    stable WHERE
    conflict/storage invariants
    NO duplicate source-language semantics
```

```text
bytes -> local symbol -> string_id (WHAT) -> identity_ref (WHO) -> WHERE
```

Hash is an accelerator, never identity. Exact spelling equality resolves hash
collisions.

## PARSER-V2-01

This slice establishes the clean parser boundary:

- moves `file_id` and `source_range` into narrow value headers;
- `prepared_token` carries an already-canonical `string_id`;
- `semantic_cursor_v2` has no file context, lexer, string table, or
  preprocessing dependency;
- `header_parser_v2` is separate from old `parser.cpp`;
- first supported grammar: namespace, struct declaration/definition, `int`,
  named record types, pointer/reference modifiers, and data members;
- incomplete named records are legal behind pointer/reference but illegal by
  value;
- production routing is unchanged.

First gate:

```cpp
namespace A {
    struct B {
        int X;
        B* Next;
    };
}
```

The initial span-backed semantic-token adapter was removed by
`PREPARED-FRONTEND-V2-01`. Parser V2 now reaches this grammar through compact
lexical replay; no retained semantic-token graph is part of the architecture.

## LEXICAL-SYMBOL-V2-01

The parallel lexer now has an optional V2 symbol sink. Production/oracle lexical
words remain unchanged while Parser V2 is being built.

Each physical-file lexer invocation owns one `lexical_symbol_stream_v2`:

```text
identifier occurrence
    -> lexer computes the same FNV32 hash used for keyword dispatch
    -> file-local open-addressed lookup
    -> local_symbol_id
```

Repeated spelling in one file reuses the same local symbol. A hash collision
never aliases text: local lookup verifies length and exact source bytes.

After all participating file lexers finish, the single-owner merge:

```text
collect UNIQUE file-local symbols
    -> sort by (hash, length, spelling)
    -> exact duplicate grouping
    -> string_table::intern() once per GLOBAL unique spelling
    -> local_symbol_id -> string_id resolution per file
```

The merge order is independent of worker/file completion order. `hash` is only
an accelerator/order key; exact spelling remains textual identity.

This slice deliberately does **not** alter the persisted lexical word format,
`database.bin`, `compiled.bin`, Graph, or the old production parser. The next
prepared-frontend slice will let `semantic_cursor_v2` consume lexical tokens plus
the resolved local-symbol sidecar directly, eliminating occurrence-level
`strings.intern()` from V2 replay.

## PREPARED-FRONTEND-V2-01

`semantic_cursor_v2` now consumes the real compact lexical word stream together
with the resolved file-local symbol sidecar.

The replay boundary is:

```text
lexical words
    +
local_symbol occurrence stream
    +
local_symbol_id -> string_id resolution
        |
        v
semantic_cursor_v2
        |
        v
prepared_token
    identifier already string_id
```

After the deterministic symbol merge, source bytes are not part of semantic
replay. Cursor decoding reads only lexical words and direct symbol resolution.

For every identifier token:

```text
identifier occurrence
    -> local_symbol_id
    -> string_id
```

There is no `files.content()`, substring extraction, spelling hash, or
`string_table::intern()` in the replay path.

The first integration gate now lexes and symbol-merges:

```cpp
namespace A {
    struct B {
        int X;
        B* Next;
    };
}
```

and feeds those real prepared lexical descriptors into `header_parser_v2`.
The test verifies that the string table size is unchanged across cursor replay
and parsing.

This slice remains root-only: preprocessing execution and include-stack
materialization have not moved to V2 yet. Old production parsing and persisted
lexical/database formats remain unchanged.

## PREPARED-INCLUDE-V2-01

Parser V2 now has a separate speculative physical include-closure builder.

For each deterministic frontier:

```text
known Header files
    -> prepare file acquisitions
    -> parallel read
    -> ordered publication to File Context
    -> parallel lexer + file-local symbols
    -> scan direct include candidates
    -> resolve existing physical targets
    -> next frontier
```

The closure owns compact lexical words and file-local symbols for prepared
Header candidates. It deliberately performs **no** global `string_id` merge for
speculative include files and publishes **no** semantic dependency edges.

That distinction is required for semantic correctness. For example:

```cpp
#ifdef NEVER
#include "unused.hpp"
#endif
```

must not make strings from `unused.hpp` part of final `compiled.bin` merely
because the physical file exists. Missing/invalid speculative candidates are
therefore recorded in the prepared include table instead of failing physical
preparation; only later active preprocessing may turn that record into a
diagnostic.

The first regression gate verifies both cases:

- an existing direct quoted include is resolved/read/lexed before semantic
  execution and has a prepared lexical/local-symbol view;
- a missing include under an unresolved conditional is recorded as `missing`
  while physical closure construction still succeeds.

Physical preparation does not call `file_context::add_dependency()`. Active
semantic preprocessing remains the owner of dependency publication.

This slice does not change `database.bin`, `compiled.bin`, Graph, the old
production parser, or the existing root-only `semantic_cursor_v2` replay path.

## PREPROCESSOR-V2-01

Header Parser V2 now consumes a dedicated `semantic_preprocessor_v2` stream
instead of a single-file lexical cursor.

The physical/semantic split is:

```text
prepared include closure
    -> physical files already resolved/read/lexed
    -> file-local symbols only
        |
        v
semantic_preprocessor_v2
    -> execute restricted directives
    -> canonicalize a file only when it becomes semantically active
    -> publish active dependency edge
    -> push prepared lexical frame
        |
        v
Header Parser V2
```

Supported restricted directives in this slice match the existing minimal
preprocessing contract:

```text
#define NAME [IDENTIFIER]
#undef NAME
#ifdef / #ifndef
#else / #endif
#include "..." / <...>
#pragma once
```

`#if` and `#elif` remain unsupported.

An active include performs no filesystem search, file acquisition, lexing, or
worker wait. It uses the include ordinal recorded by the prepared physical
closure. The target's file-local symbols are merged into the global string table
only when that target is actually entered.

Therefore an existing but inactive include may be physically prepared without
affecting final semantic identity:

```cpp
#ifdef NEVER
#include "unused.hpp"
#endif
```

`unused.hpp` contributes no dependency edge and none of its spellings receive a
global `string_id`.

Conversely, a missing prepared candidate becomes an error only when its include
directive is active.

The regression gates cover:

- `#define` + `#ifdef` activating an already-prepared include;
- included declarations flowing through Header Parser V2 before root parsing
  resumes;
- active dependency publication;
- an existing inactive include producing neither dependency nor global strings;
- an active missing include failing only during semantic preprocessing.

`semantic_cursor_v2` remains a single-file compact lexical decoder. The new
preprocessor owns only semantic directive state and the active include stack.


## GRAPH-RESOLVED-V2-01

Parser V2 now has an explicit resolved producer contract into Graph.

The defensive APIs remain available:

```text
define_record()
add_object()
derive()
```

The defensive record/object paths continue to validate source-language
construction/reference semantics for legacy callers and tests. Sparse
`graph_delta::derive()` already acts as a canonical structural constructor; the
resolved alias does not add another semantic walk.

Parser V2 uses:

```text
define_resolved_record()
add_resolved_object()
derive_resolved()
```

The resolved APIs still enforce Graph storage invariants, handle validity,
canonical identity/WHERE conflicts, normalized construction encoding, and exact
redefinition equality. They do not repeat the Parser's construction
compatibility or reference-binding semantic decisions.

`graph` also owns a transient construction-time member-name index:

```text
(type_handle, string_id)
        -> member_index
```

`find_member()` no longer scans a record's member range. The index is an
accelerator over canonical member records and is not persisted as another
semantic representation.

`graph_delta` uses the same transient index for patched/appended definitions.
Untouched baseline types delegate directly to the existing persisted
`compiled_project_view::find_member()` index, so sparse BUILD does not
materialize baseline members.

The index is intentionally construction-only:

```text
REBUILD/PUBLISH graph       -> transient member index
BUILD changed/new members   -> transient delta member index
BUILD untouched baseline    -> mmap compiled.bin member index
compiled.bin format         -> unchanged
```

Header Parser V2 now performs direct declarator legality before calling
`derive_resolved()`, and record commit uses `define_resolved_record()`.


## HEADER-V2-PARITY-01

Header Parser V2 now matches the stable-WHERE forward-declaration contract.

```text
struct B;
    -> resolve/create WHO(B)
    -> no Graph WHERE(B)

B* / B&
    -> named type expression carries WHO(B)

struct B { ... };
    -> materialize/reuse WHERE(B)
```

A direct by-value member still requires a complete record definition. A named
type used before any declaration is still unknown.

Data-member duplicate detection is record-local and ephemeral. Based on the
measured Graph lookup crossover, records with up to 32 already-parsed members
use a contiguous scan. On the 33rd member the parser promotes that one record to
an open-addressed name set. The set dies immediately after record commit and is
not a Parser-wide semantic cache.

```text
0..32 members    -> contiguous duplicate scan
33+ members      -> record-local hash set
record committed -> temporary set destroyed
```

This does not change `compiled.bin`, `database.bin`, Graph identity, stable
WHERE, the old production parser, or Source Parser behavior.



## HEADER-V2-PARITY-02

Header Parser V2 expands the record grammar without crossing the prepared
frontend boundary.

Supported in this slice:

```text
struct / class / union definitions and WHO-only forward declarations
public / protected / private member access
leading/trailing const / volatile
pointer cv qualifiers
bounded decimal arrays, including multiple dimensions
integer / boolean default member initialization
one managed default constructor:
    T();
    T() = default;
    T() : member(value) { member = value; }
```

Numeric spelling is normalized in physical preparation. `pp_number` values are
recorded in a file-local `prepared_literal_stream_v2`; semantic replay receives
the normalized value in `prepared_token`. Header Parser V2 therefore does not
read source bytes, hash spelling, or call the filesystem in order to parse array
bounds or integer initializers.

```text
source bytes
    -> physical lexical words
    -> prepared literal sidecar
    -> semantic_cursor_v2
    -> prepared_token.number
    -> Header Parser V2
```

Unsupported numeric spellings remain an invalid prepared numeric value and only
become a Parser diagnostic if that token is semantically consumed.

Record-key tracking remains semantic and WHO-based. `struct` and `class` are one
non-union family; `union` conflicts with that family before any WHERE is
materialized.

The current old record grammar accepts one data declarator per declaration.
V2 therefore also rejects `int a, b;` instead of silently widening the source
language.

Method/operator ABI declarations, inheritance, aggregate/nested constructor
initialization, reference binding and richer literal forms remain later parity
work.

This slice does not change the old production parser, Graph persistence,
`compiled.bin`, `database.bin`, or stable-WHERE rules.

## HEADER-MULTI-BASE-01 — Multiple nonvirtual bases and MSVC empty bases

OLD Header and Parser V2 consume the same comma-separated base-specifier list.
Each base retains its semantic WHO, declared order, and access. Virtual bases
are still unsupported; direct duplicate bases and incomplete bases are rejected.
The Graph `base_record` representation and persisted ABI are unchanged.

Windows Runtime construction-only layout uses per-base empty classification and
nonvirtual extent, instead of rejecting multiple empty bases. This preserves
MSVC's *default* (legacy) empty-base layout, not the opt-in
`__declspec(empty_bases)` layout (the restricted grammar has no such attribute).
The temporary type layout slot remains 16 bytes. Tests compare Graph and Parser
results, and on native MSVC compare `sizeof`, `alignof`, direct base pointer
offsets, and member offsets against actual compiler-generated objects.

## HEADER-V2-VIRTUAL-01 — Declaration-only virtual methods

V2 now accepts zero-parameter method declarations (`()` or `(void)`), with
`virtual`, `const`/`volatile`, plain `noexcept`, and `= 0`/`= default`/`= delete`.
Like OLD, declarations without bodies do not create instance data members;
explicit virtual or pure method declarations contribute the Graph polymorphic
flag. Pure declarations without an explicit virtual keyword require an existing
polymorphic base. `void` is supported as a return type; void data members are
rejected. Parameter lists with actual parameters, operator overloads,
`override`/`final`, method bodies, noexcept expressions, and other method
syntax remain unimplemented by V2.

The independent OLD/V2 differential projection now compares polymorphism and
base descriptors, in addition to member types and constructors. No Graph ABI,
compiled.bin, stable-WHERE, or production parser changes.

## Next

1. Header grammar parity.
2. Source Parser V2 parity.
3. Switch production only after Graph/diagnostic parity and benchmark acceptance.
