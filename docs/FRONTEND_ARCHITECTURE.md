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

## Project Composition and Frontend Boundary

Each parsed `project.json` is a dynamic producer of physical `file_id`
registrations and Project-declared dependency edges. Composition reads only
Project configuration files because child `project.json` bytes are required to
continue recursive discovery.

```text
project.json
    -> resolve/register all direct inputs
    -> stage Project-declared file edges
    -> parallel prefetch child project.json
    -> parse child project.json
    -> continue discovery
```

Header, Source, and Assign bytes are not acquired by `manifest_composer`.
After composition reaches closure, REBUILD owns the next construction stage.

Initial Header/Source lexical construction scans the dense `file_id` table:

```text
file_id table
    -> Header/Source only
    -> prepare acquisition
    -> worker: read -> lex private snapshot
    -> single-owner File Context publication
    -> lexical_generation.publish(file_id, stream)
```

`lexical_generation` is direct-indexed construction storage:

```text
lexical_record[file_id - 1]
    -> { offset, word_count, token_count }

one uint32 word arena
    -> all published lexical words
```

There is no manifest-to-frontend dependency, no per-file heap-owned lexical
vector in the retained generation, and no second file identity domain. Project,
Source, and Assign declarations are rejected on duplicate physical path during
composition; repeat Header references remain valid. The storage may grow when
later preprocessing discovers a new Header `file_id`.

## Parallel Per-File Lexical Stream

Physical lexing is independent per `file_id` and may run concurrently across Project files. Each worker reads immutable file bytes and writes only its private `lexical_stream`; retained construction storage is published afterward into one shared lexical word arena.

Base token representation:

```text
[ token_kind : 8 ][ source-start delta : 16 ][ source length : 8 ]
```

The payload stores a 16-bit byte delta from the previous token start plus an 8-bit lexeme length. `0xffff` and `0xff` are in-band escapes: the full 32-bit delta and/or full 32-bit lexeme length immediately follow the token header in the same word stream. There is no checkpoint array or extended-token side table.

The lexer performs no `string_table` lookup and creates no `string_id` or `identity_ref`. It directly classifies fixed C++ keywords, punctuation/operators, and preprocessing directive names. Directive lines use `pp_*` markers plus `pp_end`; direct quoted/angled include names use dedicated token kinds.

Current fail-closed lexical boundaries are non-ASCII identifiers and backslash-newline source splicing. Malformed comments, literals, and direct header names fail construction.

## `#include`

`#include` changes physical input but does not create or change semantic scope.

The directive boundary preserves the two C/C++ include forms:

```cpp
#include "x/a.hpp"  // include_form::quoted
#include <x/a.hpp>  // include_form::angled
```

At the exact directive position the directive executor emits a transient request
carrying the source `file_id`, include form, and locator.

That request belongs to directive execution, not to `frontend_input`, whose
responsibility remains only physical input traversal.

The locator is consumed synchronously and is not stored in dependency topology.

Construction resolves the request according to the include-search policy:

```text
quoted
    current physical file directory
    -> configured include roots

angled
    configured include roots
```

The include-root configuration/resolver is a separate construction policy and is
not defined by `frontend_input`.

Execution is:

```text
parent frontend reaches #include
    -> consume directive in parent
    -> include_request { source, form, locator }
    -> construction resolves/registers target file_id
    -> File Context stages source -> target
    -> target bytes are materialized
    -> frontend_input.enter(target)
    -> lexer/preprocessor/parser continue on child
    -> child EOF
    -> frontend_input.leave()
    -> parent resumes at saved byte offset
```

There is no:

```text
scan includes pass
    -> finalize dependency information
    -> parse source again
```

File dependency topology is produced as a side effect of the same streaming
frontend execution that performs preprocessing/parsing.

Example:

```cpp
namespace AA {
#include "A.hpp"
}
```

At the directive:

```text
current semantic scope = AA
```

The child input executes under that same semantic scope. If `A.hpp` contains:

```cpp
struct B {};
```

Semantic creates:

```text
identity_ref(AA::B)
```

At child EOF the input frame is popped and the parent continues.

---

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

17. Do not run a separate include/dependency prepass. Executed includes stage
    topology during the same streaming frontend execution.

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
include request resolution
include path resolver / configured include roots
#include traversal through the frontend_input boundary
construction-owner diagnostic presentation for directive_execution_error
Semantic identity_space
identity_ref integration
Graph semantic construction
```

---

## Next Construction Step

The next slice is construction-owned include execution:

```text
include_request
    -> resolve physical path
    -> resolve/register file_id
    -> File Context add_dependency(source, target)
    -> materialize/lex target if needed
    -> frontend_input.enter(target)
```

It must continue the same mutable preprocessor state through the included file.
Conditional execution state remains structurally file-bound: before
`frontend_input.leave()`, construction calls `directive_executor::finish_file()`
so an included file cannot close or leave open a conditional group belonging to
another physical file.

Filesystem, file identity, and dependency-topology ownership must not move into
`directive_executor`.
