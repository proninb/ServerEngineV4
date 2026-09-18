# Project Configuration

## Purpose

`project.json` is the ordered construction-input contract consumed by REBUILD and
by BUILD only when byte identity cannot prove the configuration unchanged.

It is not resident Project state.

## Acquisition and Streaming

The configuration path is:

```text
stable project.json snapshot
    |
    +-- SHA-256 content hash
    |
    `-- exact same bytes
            |
            v
        generic JSON parser
            |
            v
        Project ordered-schema state machine
            |
            v
        composition consumer
```

The parser never reopens `project.json`.

There is no required intermediate `project_configuration` tree.

The current schema layer validates the stream. Composition attaches directly to
this boundary.

## Version

```jsonc
"version": 1
```

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

Canonical root order:

```text
version
name
project
configuration
```

## Project Tree

Node types:

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

For `header`, `source`, and `project`, order is:

```text
name
type
path
```

A `project` node references another `project.json`. Recursive resolution belongs
to composition, not the generic JSON parser.

## Paths

Paths are relative to the `project.json` that declares them.

No directory scan is implied. Composition produces explicit roots.

## ABI

```jsonc
"configuration": {
  "abi": {
    "target": "windows-x64",
    "pack": 8
  }
}
```

Order:

```text
target
pack
```

Targets:

```text
windows-x64
posix-x64
```

Pack values:

```text
1
2
4
8
16
```

The root Project ABI is authoritative for the composed Project.

## BUILD Identity Semantics

BUILD first tries persisted physical proof:

```text
change_token
```

If unchanged cannot be proven, it reads one stable snapshot and compares:

```text
project_content_hash = SHA-256(exact bytes)
```

Only when byte content differs does BUILD stream the configuration and compute
the later semantic fingerprint.

This separates:

```text
filesystem proof
byte identity
semantic identity
```

Whitespace/comment-only changes may alter byte identity while preserving semantic
identity.

## Validation

The schema fails closed on invalid JSON, wrong field order, unknown/extra
properties, wrong types, missing required fields, invalid node types, empty
required names/paths, unsupported version, unsupported ABI target, and
unsupported ABI pack.
