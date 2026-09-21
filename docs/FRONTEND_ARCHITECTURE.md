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

`string_id` identifies one canonical interned spelling inside one BUILD lineage.

It answers:

```text
What textual spelling is this?
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

Canonical spelling contract:

```text
same spelling      -> same string_id
different spelling -> different string_id
```

Lifetime contract:

```text
REBUILD
    fresh string_id space

BUILD
    preserve every existing string_id
    append only for new spellings
```

A removed spelling does not make its slot reusable during the same BUILD
lineage. REBUILD is the compaction/reset boundary.

There is no separate `name_id`.

### `identity_ref`

`identity_ref` belongs to the construction semantic identity space.

It answers:

```text
Which semantic entity (WHO) does this name identify in this scope/domain?
```

Canonical key:

```text
(parent identity_ref, string_id, identity_kind)
    -> identity_ref
```

The supported coarse identity domains are:

```text
root
namespace_scope
type
object
```

`type` is a semantic identity domain, not a concrete declaration category.
Struct/class/union/enum/alias may all map to `identity_kind::type`.
Declaration/definition processing belongs to Parser/Semantic and G
construction; `identity_space` stores only WHO.

The implemented compact representation is:

```text
identity_ref : uint32

31        30 29                         0
+-----------+----------------------------+
| kind : 2  |          slot : 30         |
+-----------+----------------------------+
```

```text
slot 0 -> invalid
slot 1 -> root
```

The hot identity metadata is only:

```cpp
struct identity_record {
    identity_ref parent;
    string_id name;
};

static_assert(sizeof(identity_record) == 8);
```

`kind` is encoded in `identity_ref`; declaration/definition state, source
location, defining `file_id`, members, ABI state, and Graph handles are not part
of the identity record.

The kind is part of the canonical key. Therefore different semantic domains may
share one scoped spelling:

```cpp
struct A {};
A A;
```

requires two distinct identities:

```text
(root, "A", type)   -> type identity
(root, "A", object) -> object identity
```

Lifetime contract:

```text
REBUILD
    fresh identity_ref space

BUILD
    preserve every existing identity_ref
    append only for new semantic WHO values
```

An identity may remain in BUILD lineage state after its current declaration
disappears. Identity existence therefore does not mean that the entity is
currently present in `G`.

Conceptual separation:

```text
string_id
    WHAT TEXT

identity_ref
    WHO

G
    COMPILED SEMANTIC RESULT

database.bin
    BUILD LINEAGE / REUSE STATE
```

`database.bin` may preserve an `identity_ref` after that identity no longer
appears in G. This is BUILD continuity, not a second semantic state.

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
    +-- identity_space
    |      owns identity_ref canonicalization
    |
    +-- Parser / Semantic
    |      owns transient parse/resolution execution
    |      writes G directly
    |
    `-- G
           owns the compiled semantic result
```

`string_table` is not owned by Preprocessor.

Preprocessor, identity construction, and Parser/Semantic use the same canonical
`string_id` domain.

Preprocessor therefore borrows:

```cpp
const string_table&
```

It must not become the owner of textual identity.

---

## `string_table`

`string_table` remains single-owner and deterministic.

It has two lifecycle modes:

```text
REBUILD
    fresh dense table

BUILD
    committed baseline view
    + append-only local overlay
```

Required properties:

```text
no mutex
no atomic allocation
no concurrent ID publication
no semantic responsibility
```

Logical storage:

```text
string records
spelling bytes
open-addressed lookup acceleration
```

BUILD must preserve baseline numeric IDs without copying/re-interning every
baseline spelling merely to process a small delta.

Lookup is conceptually:

```text
find(text)
    local overlay?
    committed baseline?
```

New spelling allocation begins after the committed baseline slot range.

The public boundary remains:

```cpp
intern(text) -> string_id
find(text)   -> string_id
get(id)      -> string_view
contains(id) -> bool
```

`string_id` creation remains private to `string_table`.

The **current implementation** is the fresh REBUILD form only. Baseline binding
and append overlay are not implemented yet.

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

The target frontend contract is one semantic execution over the effective
preprocessing input. Include execution, active conditional state, Parser input,
and semantic scope must remain one ordered stream.

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

The stream is reusable construction input for preprocessing and Parser/Semantic
work. There is no retained semantic token graph and no second source-byte
lexing pass used only to construct dependency topology.

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

The current implementation separates physical lexical preparation and a
directive-only executed-include closure. This is an implementation boundary,
not the final Parser architecture: Parser integration must not introduce a
second independent preprocessing execution over the same semantic root.

### Stage 1: physical lexical state and current executed-include closure

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
    -> compact lexical state
```

A syntactic direct include is therefore always represented lexically, including
one inside an inactive conditional branch.

The current directive-only closure runs afterward over that lexical state. Every
frontend root gets fresh mutable preprocessing state initialized from the one root
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

The same physical `file_id` is lexed once in the construction operation.
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

### Stage 3: Parser / Semantic -> G, Assign Resolution, Topology Finalization

Parser/Semantic consumes retained lexical state without lexing source bytes
again and writes the compiled semantic result directly into `G`. There is no
intermediate facts layer, Semantic DB, SourceContribution layer, or Builder
stage.

Parser integration must preserve one effective preprocessing execution per
semantic root: ordinary active tokens flow to Parser/Semantic, while active
includes synchronously enter the included physical input under the same
preprocessor and semantic scope.

After the required identities/objects exist in G, the Assign syntax domain can
parse its materialized bytes, resolve references, write the resolved connection
into G, and stage any additional file-level dependency relations. Only then may
the construction coordinator call `file_context::finalize_dependency_topology()`
exactly once.

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

## BUILD Frontend Reuse

REBUILD constructs physical lexical state from the current source closure.

BUILD instead begins from the committed SourceSave/DB baseline.

The physical dirty set is detected before semantic processing. The old committed
reverse file topology then produces the affected closure.

For a dirty file:

```text
new exact bytes
    -> re-lex complete physical file
    -> new sparse directive anchors
```

For an unchanged but affected file:

```text
reuse persisted exact bytes
reuse persisted lexical words/directive anchors
    -> rerun only required preprocessing / Parser / Semantic work
    -> G
```

The compact lexical stream is therefore persisted BUILD acceleration data. It is
not resident runtime state and does not become a semantic token graph.

BUILD must preserve deterministic identity assignment. Identity-producing
publication remains owned by deterministic construction order; worker count must
not change `file_id`, `string_id`, or `identity_ref` values.

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

File Context owns the logical direct file topology.

All syntax producers resolve dependencies to:

```text
file_id -> file_id
```

There is no secondary node or edge identity.

The logical views are:

```text
dependencies(file_id)
dependents(file_id)
```

### REBUILD

REBUILD stages all direct relations through:

```cpp
file_context::add_dependency(
    file_id source,
    file_id target)
```

After all syntax domains reach closure, one terminal
`finalize_dependency_topology()` may build exact compact forward/reverse arenas
with the existing `O(F + E)` counting/dense-marker algorithm.

### BUILD

BUILD begins from the committed direct topology.

Order is mandatory:

```text
dirty files
    -> affected closure through OLD dependents
    -> recompute affected dependency relations
    -> current construction topology
```

BUILD must not perform a complete topology rebuild solely to discover what was
affected.

The physical sparse-update encoding is not frozen yet; the logical topology is
still one File Context/SourceSave topology.

After successful coordinated commit, the resulting topology is stored in the
new committed baseline.

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

13. File Context is the sole logical owner of direct file dependency topology.

14. A retained per-file lexical stream is BUILD/REBUILD construction data, not
    a semantic token graph.

15. The common lexical token is exactly four bytes and carries no string_id or
    identity_ref.

16. Do not store state that is authoritatively derivable elsewhere.

17. Do not introduce manager/context abstractions without a demonstrated
    ownership or lifetime requirement.

18. Active frontend include traversal is bounded execution state and must not
    allocate from the heap on include enter/leave.

19. Active frontend frames store no pointer/span into lexical_generation.

20. REBUILD creates fresh file_id/string_id/identity_ref spaces.

21. BUILD preserves existing file_id/string_id/identity_ref values and appends
    only new values.

22. identity_ref canonicalization key includes semantic kind.

23. identity_ref owns WHO only; declaration/definition processing belongs to
    Parser/Semantic and G construction, not identity_space or a Semantic DB.

24. BUILD reuse state is persisted DB/SourceSave state, never resident Project
    runtime state.

25. BUILD affected closure is computed from the committed old reverse topology
    before dependency replacement.

26. V4 has no Graph-generation model. LOAD, BUILD, and REBUILD each produce or
    restore one `G`; persistence reuse does not create G0/Gn/Gn+1 semantics.

27. There is no facts -> Semantic DB -> Builder -> G pipeline. Parser/Semantic
    writes G directly.

## Current Implemented Slice

Implemented:

```text
string_id
    4-byte canonical textual identity
    current fresh-construction implementation

string_table
    single-owner interning
    dense IDs
    immutable spelling bytes
    open-addressed index
    no mutex/atomics
    BUILD baseline binding not implemented yet

preprocessor
    borrowed const string_table&
    sparse active define table
    restricted object-like definitions
    recursive identifier expansion
    cycle protection

frontend_input
    fixed active include stack
    compact lexical token decoder
    no heap allocation on include enter/leave

directive_decoder / directive_executor
    retained directive ranges
    deterministic conditional/include execution

lexical_token
    one 32-bit common word
    in-band extensions for large source delta/length

REBUILD source closure
    deterministic dense file_id discovery
    exact Header/Source bytes
    retained lexical generation
    sparse directive anchors
    executed quoted-include closure

Assign input materialization
    exact immutable bytes only
```

Implemented physical BUILD analysis foundation:

```text
source_save_view
    zero-copy source.bin baseline view

scan_source_save_changes()
    native change-token fast path
    SHA-256 fallback
    exact dirty file_id set

collect_source_save_affected()
    OLD reverse-topology closure
    deterministic
    no sort
```

Architecture still not integrated/implemented:

```text
BUILD File Context sparse mutation over source_save_view
BUILD string_table / identity_ref lineage reuse
Parser/Semantic -> G
Assign grammar / semantic reference resolution -> G
BUILD affected dependency replacement
REBUILD terminal dependency finalization at the final producer boundary
authoritative A/B multi-artifact commit
compiled-G persistence
LOAD compiled-G restore
```

## Current Semantic Identity Foundation

The fresh-REBUILD `identity_space` is implemented.

```text
identity_ref
    32 bits
    [kind:2][slot:30]

slot 0
    invalid

slot 1
    root

identity_space
    deterministic single-owner allocation
    canonical (parent, string_id, kind) lookup
    dense 8-byte identity_record[]
    compact kind sidecar
    open-addressed construction index
    no mutex
    no atomics
```

`identity_space` borrows the construction `string_table` only to validate that
incoming `string_id` values belong to the same construction. It owns no
declaration/definition state and no persistence policy.

Root is intrinsic slot `1` stored in-place rather than allocated in the dense
vectors. Construction therefore cannot silently lose the root on allocation
failure; dense record/kind storage begins at slot `2`.

`database.bin` persists the identity lineage canonically: root slot `1` is
implicit, while slots `2..N` store only parent, `string_id`, and kind. The
construction open-addressed lookup index is deliberately not persisted.

The next semantic construction work is Parser/Semantic writing `G` directly.
There is no intermediate semantic database or Builder boundary. BUILD baseline
binding/reuse remains separate later work.

