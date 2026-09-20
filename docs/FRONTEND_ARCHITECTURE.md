# Frontend Architecture

## Purpose

This document defines the construction-time frontend architecture for Server Engine V4.

The frontend is intentionally streaming and separates three different identity domains:

```text
file_id
    physical construction input

string_id
    canonical interned spelling

identity_ref
    scoped semantic entity
```

The architecture must remain deterministic, compact, and free of retained token graphs or duplicated identity systems.

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

## Streaming Frontend

The intended frontend remains streaming:

```text
file bytes
    |
    v
lexer
    |
    +-- directive
    |      -> Preprocessor / File Context
    |
    `-- normal token
           -> Parser
           -> Semantic
```

Tokens are transient.

The architecture does not require retained:

```text
tokens[]
preprocessed_tokens[]
parsed_source[]
source_interface[]
token graph
```

The memory target is:

```text
active input stack
small lexer/parser lookahead
preprocessor state
semantic construction state
```

Frontend token memory must not scale with the total number of tokens in the Project.

---

## `#include`

`#include` changes physical input.

It does not create or change semantic scope.

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

The Streaming Frontend will:

```text
resolve include path through File Context
    -> file_id(A.hpp)

File Context:
    add_dependency(current_file, file_id(A.hpp))

push included input
continue Parser with current semantic scope AA
```

If `A.hpp` contains:

```cpp
struct B {};
```

Semantic creates:

```text
identity_ref(AA::B)
```

At EOF, the included input is popped and parent input resumes.

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

14. No retained token graph unless measurement proves it necessary.

15. Do not store state that is authoritatively derivable elsewhere.

16. Do not introduce manager/context abstractions without a demonstrated
    ownership or lifetime requirement.
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
```

Not implemented yet:

```text
streaming input stack
lexer
directive parser
#ifdef / #ifndef / #else / #endif execution
#include execution
Semantic identity_space
identity_ref integration
Graph semantic construction
```

---

## Next Construction Step

The next architectural slice is the Streaming Frontend input/lexer boundary.

It should introduce only the state required to:

```text
read one active file
produce transient lexical tokens
recognize directive position
push/pop included files later
preserve parser semantic scope across includes
```

It must not introduce:

```text
retained token arrays
source_interface
per-file semantic cache
AST persistence
generic frontend manager/context
```

The first consumer relationship should become:

```text
Streaming Frontend
    |
    +-- File Context&
    +-- string_table&
    `-- preprocessor&
```

Semantic should be added only when the lexical/directive boundary is established.
