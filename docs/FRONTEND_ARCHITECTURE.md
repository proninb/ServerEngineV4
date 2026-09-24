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

Physical lexing may retain one compact construction-only lexical stream per physical file. That stream is not a semantic graph and carries no textual or semantic identity. BUILD persistence pairs the stream with the exact immutable Header/Source byte snapshot that produced it so unchanged semantic replay never combines lexical data from one filesystem epoch with source spelling from another.

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

G / compiled.bin
    COMPILED SEMANTIC RESULT
    STRING / IDENTITY LINEAGE OWNER

database.bin
    EXACT HEADER/SOURCE SNAPSHOT BYTES
    + RETAINED PER-FILE LEXICAL BUILD STATE
```

An identity may remain in the BUILD lineage after it no longer appears in the
live Graph arrays. That continuity is preserved in compiled.bin's semantic
string/identity sections; database.bin does not carry a second identity copy.

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
    persisted-state view
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

BUILD must preserve persisted numeric IDs without copying/re-interning every
persisted spelling merely to process a small delta.

Lookup is conceptually:

```text
find(text)
    local overlay?
    persisted state?
```

New spelling allocation begins after the persisted slot range.

The public boundary remains:

```cpp
intern(text) -> string_id
find(text)   -> string_id
get(id)      -> string_view
contains(id) -> bool
```

`string_id` creation remains private to `string_table`.

The current implementation supports both modes. BUILD binds the immutable
`compiled_project_view` as its baseline, probes a local open-addressed overlay
first, falls through to the mmap baseline index, and allocates new `string_id`
values strictly after the persisted range. Persisted spelling bytes are never
copied into the local table.

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
persisted state
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

Conditional execution keeps a parallel fixed-capacity file-entry stack. Each
entry records the conditional depth that existed when that include entry began.
This boundary is intentionally not inferred from `file_id`: recursive inclusion
may enter the same physical Header while an outer conditional from that same
`file_id` is still active. `#else`, `#endif`, and EOF validation may operate only
at or above the current entry's conditional floor.

At child EOF the child frame is removed. Parent execution resumes from its saved
word/source offsets and reacquires the parent lexical span. The active include
traversal therefore remains bounded, allocation-free execution state.

Parser/Semantic recursive scope descent has an explicit 256-level supported
limit. Input exceeding that limit fails with an `unsupported` parser diagnostic
before another recursive scope call, so source-controlled nesting cannot exhaust
the process stack.

---

## Project Composition and Source-Closure Boundary

Project composition creates the initial dense `file_id` universe and stages
Project-declared dependency edges. Child `project.json` files are materialized
during composition because their bytes are required to continue recursive
composition.

Physical lexical preparation is separate from semantic execution. There is
exactly one preprocessing/include execution per semantic root, and it is
performed by the Parser/Semantic frontend itself.

### Stage 1: initial physical lexical state

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

Stage 1 does not execute preprocessing directives and does not discover
includes. It only prepares the Project-declared Header/Source physical lexical
state.

The single preprocessing execution happens in Stage 3 together with
Parser/Semantic. Every semantic root gets fresh mutable preprocessing state
initialized from the root `preprocessor_configuration`; an included Header
continues that same mutable state and semantic scope.

```text
#ifdef X
#include "a.hpp"
#endif
```

The include token exists in physical lexical state regardless of branch
activity. Only when Stage 3 reaches it in an active branch does the semantic
frontend:

```text
deterministic Parser owner
    -> resolve/register Header file_id
    -> stage source -> target edge
    -> materialize Header bytes if first seen
    -> lex complete physical Header if first seen
    -> enter Header under the same preprocessing state and semantic scope
    -> child EOF
    -> resume parent
```

No earlier directive-only execution exists. One semantic root is therefore
preprocessed exactly once.

The same physical `file_id` is lexed once in the construction operation.
Different semantic roots may execute its directives under different mutable
preprocessor states without re-lexing its bytes.

Only active includes extend the physical dependency topology. Inactive includes
do not resolve paths, do not allocate `file_id`, and do not add edges.

Header and Source are separate frontend syntax domains. They may reuse the
same low-level lexical word encoding/storage primitives, but they do not share
one grammar or preprocessing contract.

```text
Header
    C++ preprocessing / active quoted #include
    -> C++ type semantics
    -> record declarations/definitions
    -> members / managed construction
    -> namespace-scope internal-linkage static objects required by type construction

Source
    no C++ preprocessing
    no #include
    -> consumes the completed Header Type domain
    -> Project object declarations
    -> Project object initialization
    -> static Project links
```

A Source input never becomes a C++ translation unit merely because the shared
lexer recognizes preprocessing tokens. Any preprocessing directive in a Source
input fails closed.

Namespace-scope Header `static` objects are part of Header/C++ construction
semantics, not Project Source objects. Internal linkage is semantic-root local:
the same spelling in two Header semantic roots denotes two distinct semantic
objects.

Topology remains staged through Parser/Semantic include discovery. Assign input
does not emit dependency relations. After Parser/Semantic reaches closure, the
construction coordinator calls `finalize_dependency_topology()` exactly once.

The current include-search policy in this slice supports quoted includes relative
to the including physical file. Angled includes remain fail-closed until include
roots become an explicit root Project configuration contract.

### Stage 2: Assign User Table

Each `file_kind::assign` input is materialized into File Context as one immutable
exact-byte image. Acquisition is parallel in bounded `O(CPU lanes)` batches;
File Context publication remains single-owner.

The Assign parser is deliberately independent of C++ Semantic. It accepts:

```text
source<TAB>target
target=source
```

and appends normalized `{source, target}` text pairs to one ordered
`assign_table`. The table uses a compact text arena and 16-byte records. It has
no `string_id`, `identity_ref`, Graph handle, lookup/hash table, or sort.

Assign parsing performs no variable existence/type validation, no G mutation,
and no dependency-topology emission.

### Stage 3: Header Semantic -> Source Semantic -> G

Parser/Semantic consumes retained lexical state without lexing source bytes
again and writes the compiled semantic result directly into one `G`. There is
no intermediate facts layer, Semantic DB, SourceContribution layer, Builder
stage, candidate Graph, or Graph-generation object.

Semantic construction has one mandatory ordering barrier independent of
`project.json` declaration order:

```text
all Project-declared Header roots
    -> Header C++ semantic execution
    -> complete Type domain

semantic barrier

all Project-declared Source roots
    -> Source declaration semantic execution
    -> objects / object initialization / links

-> one final G
```

Header execution uses `semantic_input` preprocessing. It yields only active C++
tokens, expands the supported object-like identifier macros, and
resolves/registers active quoted includes synchronously. An include-discovered
Header is materialized and lexed once if it has not yet entered the construction
file universe, then entered under the same mutable preprocessor state and
semantic scope.

Source execution reuses retained lexical words and canonical string/identity/G
state but does not initialize or execute the C++ preprocessor. Source
preprocessing directives, including `#include`, fail closed.

Parser/Semantic writes normalized construction values directly into G.
Member defaults, constructor initializer-list operations, constructor-body field
assignments, and supported object initializers survive only as compact
`construction_value` records; source spans do not survive this boundary.

Managed constructor syntax is folded before `G.define_record()`. A reference
member may bind either to another member of the same record or to a visible
namespace-scope Header `static` object. The latter is stored as an
`object_binding` to the existing `object_handle`. No constructor representation
or executable constructor program survives in G.

Namespace-scope Header `static` objects use the normal Graph object storage but
carry internal-static storage semantics and their own persisted object
construction value. They are root-local internal-linkage entities and are not
visible through the Source object namespace.

Source Project objects also retain their supported initialization value in the
sparse cold object-construction arena. `object_entry` remains the compact hot
type/state record.

Static Project links belong only to the Source semantic line. They are resolved
to `object_endpoint` pairs and stored directly in G.

Parser integration must preserve one effective preprocessing execution per
semantic root: ordinary active tokens flow to Parser/Semantic, while active
includes synchronously enter the included physical input under the same
preprocessor and semantic scope.

Assign is not a semantic producer and never waits for identities/objects in G.
Once Parser/Semantic has completed active include discovery, no remaining Assign
dependency producer exists. The construction coordinator may therefore call
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


## Semantic Root Source Provenance

Parser/Semantic writes G directly and records source provenance in parallel.
There is no facts/Semantic-DB/Builder layer between Parser and G.

Canonical ownership is root-contiguous:

```text
semantic root file_id
    -> contiguous contribution range

contribution
    = { physical file_id, semantic datum }
```

The same physical/header datum contributed by two semantic roots therefore
occupies two contribution records. They are different ownership facts even when
their `{physical file_id, semantic datum}` payloads are equal.

Within one root/file pair, repeated identical contributions collapse during
construction. For one type in that same root/file pair, a definition dominates
a declaration. This root-local suppression uses one construction-only lookup;
there is no global cross-root contribution canonicalization.

After all roots are parsed, `source_map::finalize()` derives the physical-file
projection in O(F + C) without sorting:

```text
physical file_id
    -> uint32 contribution indices
```

That index references the canonical root-owned array and is the only Source Map
indirection persisted for provenance queries. Finalization resolves semantic
slots through `identity_space` + G's dense identity lookup instead of building a
second semantic lookup table.

Members are reached through the contributed type in G and are not stored again
in Source Map.

Parser records provenance only after successful G operations and calls
`sources.finalize(files.size(), identities, G)` after active include discovery.
Finalization validates Graph references and computes BUILD semantic presence from
the root-owned contributions. `compiled.bin` v3 persists contributions, root
ranges, the physical secondary index, and file paths/kinds; `source.bin` v4 stores
only type/object/link presence counters beside the existing file DAG.

The file dependency DAG remains direct `file_id -> file_id` topology only.
Semantic provenance does not create DAG edges.


## Persisted BUILD Path Lookup

`source.bin` v4 persists the File Context path lookup accelerator as an
open-addressed `{fingerprint, file_id}` table. The fingerprint is computed from
the platform filesystem-equivalence key and is never accepted as identity:
`source_save_view::find_path()` confirms exact key equality before returning the
existing `file_id`.

REBUILD constructs this table directly in the final `source.bin` mapping; the
layout owns only its count/offset and never materializes a full transient copy.
This removes both the extra persistence copy and the O(F) path-index
reconstruction that would otherwise be required before sparse BUILD can resolve
quoted includes. BUILD File Context now binds the committed SourceSave directly:
persisted path/kind/topology reads remain mmap-backed, touched existing files get
sparse physical/content state, and new physical identities append after the
committed `file_id` range.

Affected preprocessing sources replace their complete outgoing adjacency
sparsely. BUILD compares only those old/new edge sets and records reverse
add/remove deltas; unrelated DAG nodes and edges remain in the SourceSave mmap.

## BUILD Frontend Reuse

REBUILD constructs physical lexical state from the current source closure.

BUILD instead begins from the persisted SourceSave/DB state.

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

`lexical_generation` now has two storage modes:

```text
REBUILD
    fresh dense native records + lane arenas

BUILD
    immutable database.bin source + lexical baseline
    + single-owner masking of semantic_changed Header/Source records
    + parallel sparse lexical replacement from already-acquired changed bytes
    + append-only records for new file_id values
```

The frontend reads both through alignment/endian-safe lexical value views.
`begin_replacement(file_id)` masks the persisted entry before lexing, so failed
or incomplete replacement work cannot silently fall back to stale tokens.

`frontend_input` resolves the word view once when a file frame is entered.
Persisted views point directly into immutable `database.bin`; native views retain
`lexical_generation + arena + offset + count` and resolve the arena's current
backing allocation on indexed reads. Suspended include frames therefore survive
arena growth without storing invalidated native spans, while the token hot path
avoids repeated replacement-index and database-record lookup.

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
new persisted state.

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
    preserved across one BUILD lineage

string_table
    single-owner interning
    fresh REBUILD storage or compiled.bin baseline + local overlay
    baseline spelling bytes remain mmap-backed
    new IDs append after persisted slots
    open-addressed local index + mmap baseline index
    no mutex/atomics

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

REBUILD physical source preparation
    deterministic dense initial file_id set from Project composition
    exact Project-declared Header/Source bytes
    retained lexical generation
    sparse directive anchors
    no preprocessing execution
    active includes discovered only by Parser/Semantic

Assign user table
    exact immutable input bytes
    source<TAB>target | target=source
    ordered compact {source,target} records
    no semantic resolution / G mutation / dependency edges

Source Map construction
    semantic-root ownership
    physical file_id on every contribution
    type declaration/definition, object, link only
    no member duplication
    no sort
    O(F + C) reverse physical-file index
```

Implemented physical BUILD analysis foundation:

```text
source_save_view
    zero-copy persisted source.bin view

scan_source_save_changes()
    capture next checkpoint before dirty detection
    one Windows USN journal pass from committed checkpoint
    persisted file/directory identity indexes
    dense dirty bitset, ascending file_id without sort
    portable full-scan fallback clears next checkpoint

collect_source_save_affected()
    OLD reverse-topology closure
    deterministic
    no sort
```

Architecture still not integrated/implemented:

```text
BUILD File Context sparse mutation over source_save_view
BUILD per-file lexical replacement/reuse over database_view
BUILD affected dependency replacement
BUILD affected Parser/Semantic reconstruction into G
BUILD successful-state persistence commit boundary
Runtime/SHM construction
```

## Current Semantic Identity Foundation

`identity_space` supports fresh REBUILD storage and BUILD baseline+append
overlay storage.

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
incoming `string_id` values belong to the same construction. In BUILD mode its
persisted identity records stay in `compiled_project_view`; only new identities
enter the local dense record/kind arrays and local open-addressed index. It owns
no declaration/definition state and no persistence policy.

Root is intrinsic slot `1` stored in-place rather than allocated in the dense
vectors. Construction therefore cannot silently lose the root on allocation
failure; dense record/kind storage begins at slot `2`.

`compiled.bin` persists identity lineage canonically together with the
String Table and G. BUILD now binds those mapped semantic sections as the
baseline for append-only string/identity overlays. `database.bin` deliberately
contains no semantic identity copy; it retains exact Header/Source snapshot bytes
paired with per-file lexical state.

Parser/Semantic now writes `G` directly for the implemented namespace,
record/member, Project-object, construction, managed-constructor, and static-link
slice. There is no intermediate semantic database or Builder boundary. BUILD
persisted-state binding/reuse remains separate later work.
