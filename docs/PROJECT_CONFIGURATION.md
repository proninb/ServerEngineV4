# Project Configuration

## Purpose

`project.json` is the persistent input contract for Project construction.

It describes:

```text
Project identity
Project tree
explicit Project items
nested Project references
ABI configuration
```

It is consumed by BUILD and REBUILD construction paths.

The resident `project` must not retain the Project configuration tree after
publication unless a runtime requirement explicitly needs a derived value.

## File

The Project entry file is:

```text
project.json
```

Paths referenced by a Project configuration are resolved relative to the
directory containing that `project.json`.

No full Project-directory scan is implied by the configuration.

## Version

Current schema version:

```jsonc
"version": 1
```

Unsupported versions fail closed.

## Root Contract

Canonical root structure:

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

The Project configuration is an ordered contract.

Canonical root field order:

```text
version
name
project
configuration
```

## Project Tree

`project` contains the logical Project tree.

Node types:

```text
group
header
source
project
```

`group` is the only container type.

`header`, `source`, and `project` are leaf/reference nodes.

## group

Canonical form:

```jsonc
{
  "name": "Types",
  "type": "group",
  "children": [
  ]
}
```

Canonical field order:

```text
name
type
children
```

Contract:

```text
name      required, non-empty
type      "group"
children  required array
path      not allowed
```

Groups may contain groups and leaf/reference nodes recursively.

## header

Canonical form:

```jsonc
{
  "name": "base.hpp",
  "type": "header",
  "path": "types/base.hpp"
}
```

Canonical field order:

```text
name
type
path
```

Contract:

```text
name      required, non-empty
type      "header"
path      required, non-empty
children  not allowed
```

## source

Canonical form:

```jsonc
{
  "name": "model.hpp",
  "type": "source",
  "path": "src/model.hpp"
}
```

Canonical field order:

```text
name
type
path
```

Contract:

```text
name      required, non-empty
type      "source"
path      required, non-empty
children  not allowed
```

`source` is a Project semantic role. It is not required to mean a `.cpp` file.

## project

A `project` node references another `project.json`.

Canonical form:

```jsonc
{
  "name": "Subsystem",
  "type": "project",
  "path": "subsystem/project.json"
}
```

Canonical field order:

```text
name
type
path
```

Contract:

```text
name      required, non-empty
type      "project"
path      required, non-empty
children  not allowed
```

The single-file Project configuration loader parses exactly one `project.json`.

Recursive nested-Project processing belongs to Project composition, not to the
JSON loader.

## Composition

Project composition resolves `project` references recursively.

```text
project_configuration_loader
    |
    v
one project_configuration
    |
    v
composition resolver
    |
    +-- group
    +-- header
    +-- source
    `-- project -> referenced project.json
```

Composition requirements:

```text
normalized-path deduplication
cycle detection
deterministic declaration-order traversal
explicit roots only
```

The filesystem is not scanned to discover Project roots.

After composition, Source Manager receives explicit roots and discovers source
dependencies recursively through supported `#include` directives.

## ABI

Canonical ABI configuration:

```jsonc
"configuration": {
  "abi": {
    "target": "windows-x64",
    "pack": 8
  }
}
```

Canonical ABI field order:

```text
target
pack
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

A child Project ABI is not merged into the root Project ABI.

## Validation

The loader fails closed on:

```text
invalid JSON
unknown property
duplicate property
wrong type
missing required field
invalid node type
invalid node structure
empty required name/path
unsupported version
unsupported ABI target
unsupported ABI pack
invalid ordered-field contract
```

JSON syntax handling remains generic in `server_engine/json`; Project schema
validation belongs to the Project configuration layer.

## BUILD and REBUILD Boundary

`project.json` is construction input:

```text
project.json
    |
    v
project_configuration
    |
    v
project_context
```

BUILD uses the configuration together with persisted construction/change state
to determine whether the Project changed.

REBUILD uses the configuration to construct a new G0.

LOAD restores persisted Graph/runtime state and does not run the Project
configuration tree through Source Manager change detection.
