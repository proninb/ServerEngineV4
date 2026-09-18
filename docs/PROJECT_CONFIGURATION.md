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
  "configuration": {
    "abi": {
      "target": "windows-x64",
      "pack": 8
    }
  }
}
```

Canonical root field order:

```text
version
name
project
configuration
```

## Project Tree

Supported node types:

```text
group
header
source
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

### project

```jsonc
{
  "name": "Subsystem",
  "type": "project",
  "path": "subsystem/project.json"
}
```

For `header`, `source`, and `project`:

```text
name
type
path
```

## Path Resolution

Every declared `header`, `source`, and `project` path must be relative to the
`project.json` that declares it.

Absolute/rooted paths are rejected by the schema boundary.

There is no directory scan.

A child Project reference is resolved as:

```text
declaring project.json directory
    + relative child path
    -> normalized child project.json path
```

## Recursive Composition

Composition begins at the root `project.json` and recursively follows only
`type:"project"` references.

Contract:

```text
root-first
declaration-order DFS
normalized-path dedupe
platform filesystem case semantics
cycle detection
NO SORT
```

Repeated references to the same normalized child Project do not create duplicate
manifest entries.


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

## ABI

```jsonc
"configuration": {
  "abi": {
    "target": "windows-x64",
    "pack": 8
  }
}
```

Supported targets:

```text
windows-x64
posix-x64
```

Supported pack values:

```text
1
2
4
8
16
```

The root Project ABI is authoritative for the composed Project.

## Per-file Identity

Each participating `project.json` produces:

```text
project_content_hash
    SHA-256(exact bytes)

file_change_token
    optional filesystem unchanged proof
```

The token is an optimization only.

If token proof is unavailable, BUILD falls back to a stable read and content
hash comparison.

## Complete Configuration Identity

Each manifest entry contains:

```text
normalized path relative to root Project directory
per-file SHA-256
optional change token
```

The complete configuration hash is:

```text
project_configuration_hash =
    SHA-256(
        format domain
        ordered relative paths
        ordered per-file SHA-256 hashes
    )
```

Change tokens do not participate in this hash.

This means:

```text
change token
    physical proof optimization

project_content_hash
    exact identity of one file's bytes

project_configuration_hash
    exact identity of the complete composed configuration input set

project_semantic_fingerprint
    future canonical semantic identity
```

## BUILD Fast Path

BUILD loads the manifest committed with resident Gn.

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
-> Source Manager change detection
```

If any entry differs or disappears:

```text
recompose from root
```

Full recomposition is required because one changed `project.json` may change the
set or order of child Projects.

## Manifest Artifact

```text
<root-project-dir>/
    .serverengine/
        <root-project.json filename>/
            project.manifest
```

The artifact contains:

```text
magic
format version
entry count
aggregate configuration hash

for each entry:
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
invalid path
invalid token encoding
truncated/extra bytes
stored aggregate hash != recomputed aggregate hash
```

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
unsupported version
unsupported ABI target/pack
missing referenced project.json
recursive Project cycle
```
