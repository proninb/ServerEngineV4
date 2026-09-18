# Project Architecture

## Purpose

The Project subsystem owns construction and resident execution state for the
single Project active in one Server instance.

Server lifecycle ownership remains outside the Project subsystem:

```text
server
    |
    `-- server_context
            |
            `-- project
```

At most one resident `project` exists.

```text
server_context.project == nullptr
    == UNLOADED
```

## Resident Project

`project` is the state published after a Project operation succeeds.

Target resident state:

```text
project
    |
    +-- Graph
    +-- Runtime
    `-- SHM
```

Only data required by the active Project belongs in resident `project` state.

The resident Project must not retain construction-only state such as:

```text
project_configuration tree
project_context
Source Manager
frontend/parser state
semantic construction state
Builder state
temporary dependency/build state
```

## Construction Context

BUILD and REBUILD use a temporary `project_context`.

```text
project.json
    |
    v
project_configuration
    |
    v
project_context
    |
    +-- composition
    +-- Source Manager
    +-- frontend/parser
    +-- semantic construction
    `-- Builder
            |
            v
          Graph
```

`project_context` exists before Source Manager because it is the context for the
whole Project construction operation, not Builder-local scratch storage.

After successful construction:

```text
Graph
    |
    v
Runtime
    |
    v
SHM
    |
    v
publish project
    |
    v
destroy project_context
```

`project_context` must never become process-lifetime state in `server_context`.

## LOAD

LOAD is the fast restore path.

```text
persisted Graph
    |
    v
Runtime
    |
    v
SHM
    |
    v
project
```

LOAD does not run Source Manager and does not perform Project/source change
detection.

The Project entry path is used to locate persisted Project state. LOAD must not
silently become BUILD or REBUILD.

## BUILD

BUILD is the incremental/change-detection path.

```text
project.json
    |
    v
project_context
    |
    v
Project/source change detection
    |
    +-- unchanged -> reuse persisted/current Graph
    |
    `-- changed   -> construct Gn -> Gn+1
```

Source Manager belongs to BUILD because source identity, dependency discovery,
and change detection are construction concerns.

## REBUILD

REBUILD constructs a new G0.

```text
project.json
    |
    v
project_context
    |
    v
composition
    |
    v
explicit roots
    |
    v
Source Manager
    |
    v
frontend / semantic construction
    |
    v
Graph G0
```

REBUILD does not preserve incremental Source Manager identity/state from the
previous generation.

## Publication Rule

A resident Project is published only after the selected operation succeeds
completely.

```text
UNLOADED + operation success -> LOADED
UNLOADED + operation failure -> UNLOADED
```

Failed construction must destroy all partial Project state.

## Project Startup

`server.json` selects an optional startup operation:

```jsonc
"project": {
  "path": "project.json",
  "startup": "load"
}
```

Supported startup policies:

```text
load
build
rebuild
```

`load` is the default when `startup` is omitted.

The startup policy belongs to Server process configuration. Project build
semantics belong to `project.json` and the Project subsystem.

See `PROJECT_CONFIGURATION.md` for the `project.json` contract.

## Diagnostics

One externally visible Project operation owns exactly one:

```text
operation_id
diagnostic_collection
```

Nested Project/configuration/Source Manager/parser/Builder layers append to the
same caller-owned collection.

## Architectural Invariants

1. One Server owns zero or one resident Project.
2. `server_context.project == nullptr` exactly represents UNLOADED.
3. `project` contains only resident execution state.
4. `project_context` is temporary BUILD/REBUILD state.
5. LOAD does not perform Source Manager change detection.
6. BUILD performs change detection and incremental construction when required.
7. REBUILD constructs a new G0.
8. Source Manager is construction state, not resident runtime state.
9. Failed Project operations publish nothing.
10. Runtime hot paths do not depend on construction/control-plane synchronization.
