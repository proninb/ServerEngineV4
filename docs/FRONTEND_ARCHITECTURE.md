# Frontend Architecture

## Purpose

This document defines the construction-time frontend architecture for Server Engine V4.

The construction frontend separates three different identity domains:

```text
file_id
    physical construction input

string_id
    canonical interned spelling

identity_ref
    scoped semantic entity
```

The architecture must remain deterministic, compact, and free of duplicated identity systems.

Physical lexing may retain one compact construction-only lexical stream per physical file. That stream is not a semantic graph and carries no textual or semantic identity.

---

## Core Identity Model

### `file_id`

`file_id` belongs to File Context.

It answers:

```text
Which physical file is this?
```

One normalized physical file has one `file_id`.

`file_id` is not semantic identity.

The same physical file may be parsed in different semantic scopes and therefore may contribute different semantic entities.

Example:

```cpp
namespace AA {
#include "A.hpp"
}

namespace BB {
#include "A.hpp"
}
```

If `A.hpp` contains:

```cpp
struct B {};
```

then one physical:

```text
file_id(A.hpp)
```

may produce:

```text
identity_ref(AA::B)
identity_ref(BB::B)
```

Therefore:

```text
file_id != semantic identity
```

---

### `string_id`

`string_id` identifies one canonical interned spelling inside the current construction/current Project state.

It answers:

```text
What textual name is this?
```

It is a compact four-byte identity:

```cpp
static_assert(sizeof(string_id) == 4);
```

`string_id` carries no:

```text
semantic scope
semantic kind
declaration state
preprocessor state
```

The canonical contract is:

```text
same spelling      -> same string_id
different spelling -> different string_id
```

There is no separate `name_id`.

That abstraction would duplicate the same identity domain.

---

### `identity_ref`

`identity_ref` belongs to Semantic.

It answers:

```text
Which semantic entity does this name mean in this scope?
```

Conceptually:

```text
(parent identity_ref, string_id, semantic kind)
    -> identity_ref
```

Examples:

```text
(root, "B") -> ::B
(X,    "B") -> X::B
(Y,    "B") -> Y::B
```

Therefore:

```text
string_id   = textual identity
identity_ref = semantic identity
```

---

## Construction Ownership

The intended ownership is:

```text
Construction
    |
    +-- File Context
    |      owns file_id and physical file state
    |
    +-- string_table
    |      owns string_id and canonical spelling bytes
    |
    +-- preprocessor
    |      owns active macro definitions only
    |
    +-- Streaming Frontend
    |      owns active input/parser traversal state
    |
    `-- Semantic
           owns identity_ref and semantic state
```

`string_table` is not owned by Preprocessor.

Both Preprocessor and Semantic use the same canonical `string_id` domain.

Preprocessor therefore borrows:

```cpp
const string_table&
```

It must not become the owner of textual identity.

---

## `string_table`

The V4 `string_table` is deliberately simpler than the V3 implementation.

Current architecture:

```text
single owner
no mutex
no atomics
no baseline overlay
no persistence coupling
no semantic responsibility
```

Storage:

```text
string_record[]
char bytes[]
open-addressed index[]
```

The table provides:

```cpp
intern(text) -> string_id
find(text)   -> string_id
get(id)      -> string_view
contains(id) -> bool
```

`string_id` construction remains private to `string_table`.

Consumers must not reconstruct a `string_id` from a raw integer.

This preserves the identity boundary and prevents unrelated subsystems from manufacturing textual identities.

---

## Preprocessor

The Preprocessor is construction-local state.

It owns only active object-like macro bindings.

It does not own:

```text
file traversal
string storage
semantic identity
parser scopes
Graph state
```

Current dependency:

```text
preprocessor
    borrows const string_table&
```

Current implementation intentionally contains no:

```text
mutex
atomics
baseline state
persistence state
semantic lookup
file lookup
```

---

## Sparse Macro State

Preprocessor storage must scale with the number of active macro definitions, not with the total number of interned strings.

Rejected design:

```text
definitions[string_id]
```

because it would make macro-state memory proportional to every lexical/semantic string in the Project.

Current design:

```text
sparse open-addressed define table
```

Each occupied slot stores:

```cpp
struct define_slot {
    string_id name;
    string_id replacement;
};
```

and remains eight bytes:

```cpp
static_assert(sizeof(define_slot) == 8);
```

Slot occupancy is determined by `name`.

An invalid replacement:

```text
replacement == {}
```

means an empty object-like replacement.

---

## `#define`

### Empty replacement

```cpp
#define A
```

is represented as:

```text
name        = string_id(A)
replacement = {}
```

`A` is still considered defined.

This is sufficient for normal include guards:

```cpp
#ifndef A_HPP
#define A_HPP
...
#endif
```

---

### Identifier replacement

```cpp
#define A B
```

is represented as:

```text
name        = string_id(A)
replacement = string_id(B)
```

No semantic lookup occurs at definition time.

This is essential.

`B` does not need to be a declared semantic entity when the directive is read.

---

## Recursive Identifier Expansion

Object-like identifier replacement is evaluated from the current Preprocessor state.

Example:

```cpp
#define A B
#define B C
```

Use of `A` expands as:

```text
A -> B -> C
```

The Preprocessor resolves this chain without allocation on the expansion hot path.

Macro recursion must terminate safely.

Example:

```cpp
#define A B
#define B A
```

The implementation detects the replacement cycle and terminates expansion at the cycle entry rather than recursing indefinitely.

This provides the required protection for the restricted identifier-only macro model.

---

## `#undef`

```cpp
#undef A
```

removes only the active preprocessing definition.

It does not affect:

```text
string_id(A)
semantic entities named A
file identity
Graph state
```

Undefining a valid but currently undefined name is a successful no-op.

---

## Preprocessor Name Validation

The Preprocessor accepts only `string_id` values that belong to its borrowed `string_table`.

It does not manufacture `string_id` values from raw integers.

Correct boundary:

```text
text
  -> string_table.intern(...)
  -> string_id
  -> preprocessor
```

Rejected boundary:

```text
raw uint32_t
  -> manually construct string_id
```

---

## Semantic Resolution After Preprocessing

Example:

```cpp
#define A B

namespace X {
    A value;
}

namespace Y {
    A value;
}
```

Preprocessor state:

```text
string_id(A) -> string_id(B)
```

Under namespace `X`:

```text
A
 -> Preprocessor
 -> string_id(B)
 -> Semantic.resolve(identity_ref(X), string_id(B))
 -> identity_ref(X::B)
```

Under namespace `Y`:

```text
A
 -> Preprocessor
 -> string_id(B)
 -> Semantic.resolve(identity_ref(Y), string_id(B))
 -> identity_ref(Y::B)
```

Therefore:

```text
same replacement spelling
+
different semantic scope
=
different semantic identity
```

Preprocessor never assigns semantic meaning.

---

## Preprocessor Configuration

The root `project.json` contributes one immutable `preprocessor_configuration`.
Nested Project configurations only compose inputs and cannot introduce another
preprocessing scope.

```text
root project.json
    -> preprocessor.predefines[]
    -> preprocessor_configuration
```

There is no per-Project preprocessing configuration, no inheritance/merge rule,
and no `project_id` for preprocessing.

Configuration is distinct from mutable preprocessing state. Each frontend
execution starts from the root configuration, interns its configured spellings
through the construction `string_table`, and then owns its mutable
`preprocessor` state. `#include` continues that same mutable state.

## Streaming Frontend

The frontend is a single streaming execution over the effective preprocessing
input. V4 does not perform a separate include/dependency scan before parsing.

```text
active file bytes
    |
    v
lexer / directive execution
    |
    +-- #include
    |      -> synchronous construction resolution
    |      -> push child input
    |      -> continue the same stream
    |
    `-- normal token
           -> Parser
           -> Semantic
```

The per-file lexer produces a compact construction-only lexical stream. Each base lexical token is exactly one 32-bit word:

```text
[ token_kind : 8 ][ source-start delta : 16 ][ source length : 8 ]
```

The lexical stream classifies punctuation, C++ keywords, literal classes, and preprocessing directives. It does not intern identifiers and does not create `string_id` or `identity_ref`.

The stream is reusable construction input for preprocessing and parallel type-level semantic work. There is still no retained semantic token graph and no second source-byte lexing pass used only to construct dependency topology.

The active lexical-input stack stores only:

```text
{ file_id, word_offset, source_offset }
```

`word_offset` is the next word in that file's compact lexical stream.
`source_offset` is the previous decoded token start and is required to decode the
next token's source-start delta in O(1). No checkpoint table is required.

The stack is a fixed-capacity array with a 256-level include-nesting limit.
Frames store no pointer, span, or arena address. `frontend_input::next()` obtains
a fresh `lexical_generation::words(file_id)` view for each decode, so publishing
an include-discovered Header may reallocate the lexical word arena without
invalidating a suspended parent frame.

At child EOF the child frame is removed. Parent execution resumes from its saved
word/source offsets and reacquires the parent lexical span. The active include
traversal therefore remains bounded, allocation-free execution state.

---

## Project Composition and Source-Closure Boundary

Project composition creates the initial dense `file_id` universe and stages
Project-declared dependency edges. Child `project.json` files are materialized
during composition because their bytes are required to continue recursive
composition.

Header/Source construction then runs in two distinct stages.

### Stage 1: physical lexical facts and executed-include closure

For every Project-declared Header/Source, the complete physical file is
materialized before physical lexing begins. Immutable files are partitioned into
contiguous `file_id` pools balanced by materialized byte weight. One execution
lane owns one reusable `lexical_stream` and one retained lexical arena. Worker
threads are created once for the source-closure lifetime and reused by every
physical lex run; there is no task array, shared append arena, mutex, atomic work
index, or sort.

The complete physical file is lexed to EOF before preprocessing directive
execution begins.
The lexer never pauses at `#include` and never depends on preprocessor state.
During that same pass it records a sparse directive anchor for each `pp_*`
directive start. Directive execution therefore visits only preprocessing
directives and their directive-line tokens; ordinary C++ tokens are not decoded
again during source closure.

```text
physical bytes
    -> lexer to EOF
    -> compact lexical facts
```

A syntactic direct include is therefore always represented lexically, including
one inside an inactive conditional branch.

Directive execution runs afterward over those lexical facts. Every frontend root
gets fresh mutable preprocessing state initialized from the one root
`preprocessor_configuration`; an included file continues the same mutable state.

```text
#ifdef X
#include "a.hpp"
#endif
```

`#include "a.hpp"` is always lexed/decoded. It is resolved and executed only when
the active conditional state reaches that directive.

Executed directives are traversed by one construction owner in ascending
initial `file_id` root order. An active include is resolved immediately in that
owner order. If the target Header has not been lexed yet, its immutable bytes are
materialized and the persistent lexical lanes execute the causally available
physical work before the owner enters the child:

```text
deterministic owner
    -> active include_request
    -> resolve/register Header file_id
    -> stage source -> target edge
    -> materialize immutable target bytes
    -> persistent lexical lanes
    -> complete-file lex to EOF
    -> sparse directive anchors
    -> enter child directive execution
    -> child EOF
    -> resume parent
```

The owner never advances another root while an earlier root can still discover a
new include. Therefore dense `file_id` assignment and directive-driven
`string_id` interning are independent of hardware concurrency. A single root's
include chain remains sequential because a child may change that root's mutable
preprocessing state before the parent continues.

The same physical `file_id` is lexed once in the construction generation.
Different frontend roots may execute its directives under different mutable
preprocessor states without re-lexing its bytes.

Only executed includes extend the physical dependency topology. Inactive
includes do not resolve paths, do not allocate `file_id`, and do not add edges.

After every Project-declared frontend root reaches directive closure, all
Header/Source dependency relations remain staged in File Context. Source closure
does not finalize topology. Assign bytes may be materialized next, but semantic
variable identity must exist before Assign reference resolution. The construction
coordinator calls `finalize_dependency_topology()` exactly once only after
Semantic-backed Assign resolution and every other dependency producer reaches
closure.

The current include-search policy in this slice supports quoted includes relative
to the including physical file. Angled includes remain fail-closed until include
roots become an explicit root Project configuration contract.

### Stage 2: Assign Input Materialization

After Header/Source closure, each `file_kind::assign` input is materialized into
File Context as one immutable exact-byte image. Acquisition is parallel in
bounded `O(CPU lanes)` batches; File Context publication remains single-owner.

This stage does not invent or parse an `.assign` grammar, does not resolve
variables, and does not emit dependency edges.

### Stage 3: Parser / Semantic, Assign Resolution, Topology Finalization

Parser/Semantic consumes the completed Header/Source physical universe and
retained lexical facts without lexing source bytes again. Semantic construction
establishes the variable identities required by Assign resolution.

After Semantic identity exists, the Assign syntax domain can parse its
materialized bytes, resolve variable references, and stage any additional
file-level dependency relations. Only then may the construction coordinator call
`file_context::finalize_dependency_topology()` exactly once.

`lexical_generation` remains direct-indexed construction storage:

```text
lexical_record[file_id - 1]
    -> { arena, word_offset, word_count, token_count }

lexical_directive_record[file_id - 1]
    -> { directive_offset, directive_count }

O(CPU lanes) lexical arenas
    -> one producer per arena during parallel physical lex
    -> persistent workers are created once for source closure
    -> lane arenas are reused for later physical work
```

`frontend_input` stores only active per-file lexical positions:

```text
{ file_id, word_offset, source_offset }
```

Frames keep no pointer/span into the lexical arena. If a newly discovered Header
causes lexical storage growth, suspended parent positions remain valid and
reacquire their lexical span on the next decode.

## Include Guards

Example:

```cpp
#ifndef A_HPP
#define A_HPP

struct B {};

#endif
```

included as:

```cpp
namespace AA {
#include "A.hpp"
}

namespace BB {
#include "A.hpp"
}
```

within one construction preprocessing stream:

```text
first include under AA
    A_HPP undefined
    -> define A_HPP
    -> parse body
    -> AA::B

second include under BB
    A_HPP already defined
    -> body skipped
```

No special include-guard state belongs in File Context.

It is ordinary Preprocessor state.

---

## File Dependency Topology

File Context owns dependency staging.

All syntax producers use:

```cpp
file_context::add_dependency(
    file_id source,
    file_id target)
```

There is one dependency staging owner.

No frontend/composer-local duplicate dependency vector is allowed.

After complete dependency discovery closure:

```cpp
file_context::finalize_dependency_topology()
```

builds compact forward/reverse topology.

After finalization:

```text
no new file_id
no new dependency edge
```

---

## Architectural Gates

The following contracts are fail-closed architecture rules.

```text
1. file_id never means semantic identity.

2. string_id never means semantic identity.

3. identity_ref never means preprocessing symbol identity.

4. There is no separate name_id.

5. string_table owns string_id creation.

6. Consumers do not reconstruct string_id from raw integers.

7. Preprocessor owns only active macro state.

8. Preprocessor borrows string_table; it does not own textual identity.

9. Macro-state memory scales with defined macros, not all strings.

10. #define does not perform semantic lookup.

11. Semantic resolution happens at the use site.

12. #include changes physical input, not semantic scope.

13. File Context is the sole owner of dependency staging.

14. A retained per-file lexical stream is construction data, not a semantic token graph. Its common token is exactly four bytes and carries no string_id or identity_ref. Rare large source deltas or lexeme lengths use in-band extension words in the same uint32 stream.

15. Do not store state that is authoritatively derivable elsewhere.

16. Do not introduce manager/context abstractions without a demonstrated
    ownership or lifetime requirement.

17. Source closure may execute preprocessing directives before Parser/Semantic,
    but it must reuse the one retained lexical generation. Source bytes are
    never lexed a second time only to discover dependencies.

18. Active frontend include traversal is bounded execution state and must not
    allocate from the heap on include enter/leave.

19. Active frontend frames store no pointer/span into lexical_generation. Resume
    state is file_id + word_offset + source_offset and reacquires the span.
```

---

## Current Implemented Slice

Implemented:

```text
string_id
    4-byte canonical textual identity

string_table
    single-owner interning
    dense IDs
    immutable spelling bytes
    open-addressed index
    no mutex/atomics/baseline/persistence

preprocessor
    borrowed const string_table&
    sparse active define table
    #define NAME
    #define NAME OTHER
    #undef
    defined(name)
    recursive identifier-only expansion
    cycle protection

frontend_input
    fixed 256-level { file_id, word_offset, source_offset } stack
    compact lexical token decoder
    no heap allocation on include enter/leave
    no retained lexical span across include publication
    O(1) parent resume after child include

directive_decoder
    consumes exactly one pp_* ... pp_end directive
    retains exact file-local lexical/source ranges
    typed identifier operands for define/undef/ifdef/ifndef
    direct quoted/angled include payload
    no include resolution / macro execution / File Context mutation

directive_executor
    initializes mutable state from root preprocessor_configuration
    fixed 256-level conditional stack
    conditional groups are balanced within one physical file
    #define NAME / #define NAME IDENTIFIER / #undef
    #ifdef / #ifndef / #else / #endif
    inactive-branch suppression for normal directives
    direct #include -> transient include_request
    typed execution error + exact source range
    no include resolution / File Context mutation

lexical_token
    one 32-bit word
    8-bit token_kind
    16-bit source-start delta + 8-bit lexeme length
    in-band 32-bit extension words for large delta/length
    one uint32 word stream; no side arrays
    punctuation / keyword / preprocessing classification
    no string_id / identity_ref
```

Not implemented yet:

```text
configured include roots / angled include resolution
Parser/Semantic construction
Semantic identity_space
identity_ref integration
Assign grammar and semantic variable resolution
terminal File Context dependency-topology finalization
Graph semantic construction
```

---

## Next Construction Step

The next slice is the Semantic identity foundation.

The identity domains remain strictly separated:

```text
file_id
    physical construction input

string_id
    canonical textual spelling

identity_ref
    scoped semantic entity
```

Semantic identity is canonicalized by:

```text
(parent identity_ref, string_id, semantic kind)
    -> identity_ref
```

The foundation must be construction-local, deterministic, direct-indexed by
`identity_ref`, and free of source/file ownership. Source locations,
declaration/definition state, and defining `file_id` do not belong in the hot
identity record.

Parser/Semantic later owns declaration legality and lookup policy. The identity
foundation only owns canonical scoped identity.
