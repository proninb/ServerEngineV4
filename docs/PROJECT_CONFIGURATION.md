# Project Configuration

## Purpose

`project.json` is the ordered Project construction-input contract.

It is consumed by:

```text
REBUILD
    always compose from root

BUILD
    only recompose when committed configuration byte identity changed
```

It is not resident Project state.

No materialized `project_configuration` tree is required.

## Streaming Boundary

Each participating configuration file follows:

```text
stable file snapshot
    +-- SHA-256 content hash
    `-- exact same bytes
            -> generic JSON parser
            -> ordered Project schema state machine
            -> direct child Project references
```

The parser does not reopen the file.

On a diagnostic error, ownership of the same byte buffer moves to the diagnostic
source cache. No full source-text copy is required.

## Root Contract

```jsonc
{
  "version": 1,
  "name": "Example",
  "project": [
  ],
  "preprocessor": {
    "predefines": [
      {
        "name": "PROJECT_LOCAL"
      },
      {
        "name": "ALIAS",
        "replacement": "TARGET"
      }
    ]
  }
}
```

Canonical root field order:

```text
version
name
project
preprocessor
```

Nested `project.json` field order:

```text
version
name
project
```

`preprocessor` is required only on the root configuration and forbidden on
nested configurations.

## Project Tree

Supported node types:

```text
group
header
source
assign
project
```

### group

```jsonc
{
  "name": "Types",
  "type": "group",
  "children": [
  ]
}
```

Order:

```text
name
type
children
```

### header

```jsonc
{
  "name": "base.hpp",
  "type": "header",
  "path": "types/base.hpp"
}
```

### source

```jsonc
{
  "name": "model.hpp",
  "type": "source",
  "path": "src/model.hpp"
}
```

### assign

```jsonc
{
  "name": "Connections",
  "type": "assign",
  "path": "tasks/connections.assign"
}
```

`assign` is lightweight Studio-facing user data for a special task. It does
not declare types, create runtime objects, resolve `identity_ref`, mutate G, or
create File Context dependency edges.

The current line grammar accepts either:

```text
source<TAB>target
target=source
```

Both forms normalize to the same ordered `{source, target}` record in
`assign_table`. Leading/trailing spaces and tabs around each value are removed.
Blank lines are ignored. Records preserve Project/file/line order; there is no
sort, lookup, or semantic existence check for either name.

### project

```jsonc
{
  "name": "Subsystem",
  "type": "project",
  "path": "subsystem/project.json"
}
```

For `header`, `source`, `assign`, and `project`:

```text
name
type
path
```

## File Kind and Syntax Routing

Every non-group Project item emits one ordered typed dependency:

```text
type:"project" -> file_kind::project
type:"header"  -> file_kind::header
type:"source"  -> file_kind::source
type:"assign"  -> file_kind::assign
```

Each File Context node has one immutable `file_kind`. The same physical path
cannot be classified as two syntax domains inside one construction lineage.

A common dependency graph does not imply common syntax: Project, Header, and
Source and Assign dependencies are discovered by their own language rules.

Cross-file cardinality is part of Project composition semantics:

```text
header  -> reusable
source  -> unique in one composed Project
assign  -> unique in one composed Project
project -> unique in one composed Project tree
```

A repeated Project on the active recursion stack is a cycle. A Project already
completed elsewhere in the tree is a duplicate Project error. A repeated Source
or Assign input is an error. Reusing the same physical file with a different
`file_kind` is also invalid.

These checks belong to composition itself and therefore run identically during
BUILD manifest-only recomposition and REBUILD File Context population. File
Context does not own these rules; it only owns physical identity and immutable
`file_kind`.

`file_id` belongs to one BUILD lineage, not one final Graph. REBUILD starts a
fresh File Context identity space; BUILD restores/binds committed File Context
slots so existing physical inputs keep the same `file_id` across successful
BUILDs. BUILD never renumbers or recycles historical slots.

## Dependency Semantics

Project configuration contributes direct file dependencies using the same
`file_id` identity space as every other construction input.

For a declaring `project.json`:

```text
project file_id -> child project file_id
project file_id -> header file_id
project file_id -> source file_id
project file_id -> assign file_id
```

These are file-level construction dependencies. They do not replace semantic
relations produced later by Type, Source, or Assign frontends.

`assign` is intentionally lightweight:

```text
assign file
    -> parse user connection descriptions
    -> resolve referenced variables/endpoints
    -> diagnose missing/invalid references
    -> write the resolved connection into G
```

The eventual assignment relation in Graph is distinct from the file dependency
relation used for BUILD invalidation.

The topology model does not allocate a second identity for graph nodes or edges:

```text
node identity = file_id
edge identity is implicit in (source file_id, target file_id)
```

Project composition stages only the explicit Project-declared edges in the
shared File Context dependency arena. Header, Source, and Assign frontends append
their own resolved direct dependencies in later construction stages. The compact
forward/reverse topology is finalized only after all dependency discovery reaches
closure.

## Path Resolution

`header`, `source`, `assign`, and `project` support two locator forms:

```text
relative
absolute
```

Examples:

```jsonc
{
  "name": "Shared",
  "type": "project",
  "path": "../Shared/project.json"
}
```

```jsonc
{
  "name": "InstalledShared",
  "type": "project",
  "path": "D:\\Common\\Shared\\project.json"
}
```

A relative locator is interpreted relative to the `project.json` that declares
it. A fully absolute locator is interpreted directly.

Context-dependent rooted forms that are not fully absolute are rejected. On
Windows this includes drive-relative forms such as `C:foo\\project.json`.

There is no directory scan.

For child Project composition:

```text
relative locator
    declaring project.json directory + locator
        -> resolve_project_path(...)

absolute locator
    locator
        -> resolve_project_path(...)

resolved absolute normalized path
    -> make_project_path_key(...)
```

`resolve_project_path()` and `make_project_path_key()` are fail-closed
status-returning boundaries.

## Recursive Composition

Composition begins at the root `project.json` and recursively follows only
`type:"project"` references.

Contract:

```text
root-first
declaration-order DFS
platform filesystem case semantics
cycle detection
duplicate Project rejection
NO SORT
```

Repeated references to the same child Project are invalid. Project composition
is a tree; sharing/reuse is permitted only for reusable Header inputs.

Filesystem-equivalence policy is platform-specific and isolated behind
`project_path.hpp`:

```text
Windows -> invariant Unicode case-insensitive key
POSIX   -> case-sensitive key
```

The generic composition layer contains no platform API code.

Platform key construction is fail-closed. A failure to construct the required
filesystem-equivalence key stops composition; Windows never falls back to a
case-sensitive key.

If a Project references a configuration currently active in the recursion stack,
composition fails with a cycle diagnostic.

## Preprocessor

Only the root `project.json` owns immutable preprocessing configuration.

```jsonc
"preprocessor": {
  "predefines": [
    { "name": "ROOT_FEATURE" },
    { "name": "ALIAS", "replacement": "TARGET" }
  ]
}
```

The current representation supports the restricted object-like forms:

```text
NAME
NAME -> IDENTIFIER
```

`preprocessor_configuration` is construction input only. Mutable
`#define/#undef` state belongs to one frontend execution.

Nested `project.json` files are composition only and must end after `project`.
They cannot declare `preprocessor`; no preprocessing inheritance, merge, or
`project_id` is required.

```text
root project.json
    -> one preprocessor_configuration

nested project.json
    -> composition only
```

A nested `preprocessor` property is rejected at that property's source location
with `project.invalid_configuration` and detail:

```text
preprocessor is allowed only in the root Project configuration
```

A root configuration that omits `preprocessor` is rejected at the root object
boundary with detail:

```text
root Project configuration requires fields in order: version, name, project, preprocessor
```

ABI is absent from `project.json`. Target and pack are Server-wide because one
Server owns one SHM layout contract.

## Per-file Identity

Each participating `project.json` produces:

```text
file_content_hash
    SHA-256(exact bytes)

file_change_token
    optional filesystem unchanged proof
```

The token is an optimization only.

If token proof is unavailable, BUILD falls back to a stable read and content
hash comparison.

## Complete Configuration Identity

The manifest is a minimal ordered configuration dependency graph.

Each entry contains:

```text
declaring_file
path_type
path
per-file SHA-256
optional change token
```

The entries are root-first declaration-order DFS. For every non-root entry:

```text
declaring_file < current entry index
```

The root uses:

```text
declaring_file = invalid_configuration_file
path_type = relative
path = root project.json filename
```

The manifest store enforces this root-entry contract during both encode and
decode. The root locator must be a single relative filename; malformed root
metadata is rejected at the artifact boundary before manifest verification.

No persisted resolved absolute path or platform path key is required.

The complete configuration hash is:

```text
project_configuration_hash =
    SHA-256(
        format domain
        ordered {
            declaring_file
            path_type
            normalized locator
            per-file SHA-256
        }
    )
```

`path_type` participates in identity because relative and absolute locators have
different resolution semantics.

Change tokens do not participate in this hash.

This means:

```text
change token
    physical proof optimization

file_content_hash
    exact identity of one file's bytes

project_configuration_hash
    exact identity of the complete ordered configuration dependency graph

G
    compiled semantic result
```

Relative-locator identity is relocation-stable while the composed relative
topology is preserved. Absolute locators are intentionally location-bound.

## BUILD Fast Path

BUILD starts from `UNLOADED` and receives the root Project path.

It opens the persisted configuration proof used by BUILD.

For every entry:

```text
token proves unchanged
    -> no read

token cannot prove unchanged
    -> acquire stable snapshot
    -> SHA-256
```

If every SHA-256 matches:

```text
no configuration parse
no recomposition
-> SourceSave/File Context physical change detection
```

If any entry differs or disappears:

```text
recompose from root
```

Full recomposition is required because one changed `project.json` may change the
set or order of child Projects and explicit inputs.

Recomposition exists only in temporary BUILD state. It becomes reusable only after the corresponding
persisted BUILD artifacts are successfully replaced.

## Manifest Artifact

The configuration-proof artifact currently uses:

```text
<root-project-dir>/
    .serverengine/
        <root-project.json filename>/
            project.manifest
```

It contains:

```text
magic
format version
entry count
aggregate configuration hash

for each entry:
    declaring-file index
    locator type
    path length
    flags
    content hash
    optional change token
    path bytes

artifact checksum
```

Validation fails closed on:

```text
bad magic/version
bad checksum
unknown flags
invalid locator type/path
invalid declaring-file edge
invalid token encoding
truncated/extra bytes
stored aggregate hash != recomputed aggregate hash
```

`project.manifest` is persisted BUILD acceleration state. LOAD does not require
it. BUILD requires a valid manifest together with the other BUILD state it uses;
if required persisted BUILD state is missing or invalid, REBUILD is required.

## Validation

The schema/composition layer fails closed on:

```text
malformed JSON
wrong field order
unknown/extra properties
wrong types
missing required fields
invalid node types
empty required names/paths
invalid root predefine objects
unsupported version
missing referenced project.json
recursive Project cycle
absolute path resolution failure
filesystem-equivalence key construction failure
```
