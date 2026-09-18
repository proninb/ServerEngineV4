# Project Architecture

## Purpose

Project lifecycle is mode-oriented:

```text
LOAD
BUILD
REBUILD
UNLOAD
```

There is no universal `project_context`. Each operation owns only the temporary
state required by that operation.

Server owns zero or one resident Project:

```text
server
    |
    `-- server_context
            |
            `-- project
```

`server_context.project == nullptr` exactly means UNLOADED.

## State Machine

```text
UNLOADED
    +-- LOAD success ------> LOADED Gn
    +-- LOAD failure ------> UNLOADED
    |
    +-- REBUILD success ---> LOADED G0
    `-- REBUILD failure ---> UNLOADED

LOADED Gn
    +-- BUILD success -----> LOADED Gn+1
    +-- BUILD failure -----> UNLOADED
    |
    `-- UNLOAD -----------> UNLOADED
```

Operation preconditions are strict:

```text
LOAD     requires UNLOADED
REBUILD  requires UNLOADED
BUILD    requires LOADED
UNLOAD   requires LOADED
```

No operation silently substitutes another operation.

A failure of LOAD, BUILD, or REBUILD always leaves the Server UNLOADED.

## Resident Project

Resident `project` contains only data required while the Project is active.

Target state:

```text
project
    |
    +-- Graph
    +-- Runtime
    `-- SHM
```

It must not retain `project.json`, composition state, Source Manager,
parser/frontend state, Builder state, or mode-local temporary state.

The resident Project retains its Project entry path because a subsequent BUILD
uses the active Project as its Gn input and resolves construction artifacts from
that Project location.

## LOAD

LOAD is valid only while UNLOADED.

```text
persisted Graph
    -> Runtime
    -> SHM
    -> resident Project Gn
```

LOAD does not parse `project.json` for construction, run Source Manager, or
perform Project/source change detection.

On failure no resident Project is published.

## BUILD

BUILD is valid only while a Project is already LOADED.

BUILD has no Project path argument. The current resident Project is the Gn input:

```text
resident Project Gn
    |
    +-- project path
    +-- current Graph/Runtime/SHM
    |
    v
persisted construction baseline
    |
    v
Project/configuration/source change detection
    |
    v
candidate Gn+1
```

If BUILD succeeds:

```text
candidate Gn+1
    -> commit required persisted construction state
    -> publish Gn+1
```

If BUILD fails:

```text
destroy temporary/candidate BUILD state
destroy resident Gn
server_context.project = nullptr
-> UNLOADED
```

The old Gn is not retained after a failed BUILD.

BUILD begins with persisted Project input proof. Current single-file identity is
an implementation stage; composed Project identity will become a configuration
manifest covering every participating `project.json`.

Identity levels remain distinct:

```text
file_change_token
    filesystem unchanged proof for one file

project_content_hash
    SHA-256 byte identity for one configuration file

project_semantic_fingerprint
    semantic identity of the composed Project configuration
```

## REBUILD

REBUILD is valid only while UNLOADED and constructs a fresh G0.

```text
project.json
    -> complete composition
    -> explicit roots
    -> new Source Manager
    -> frontend / semantic construction
    -> Graph G0
    -> Runtime
    -> SHM
    -> resident Project G0
```

REBUILD ignores incremental construction state.

A successful REBUILD creates/persists the construction baseline required by
future BUILD operations.

On failure the Server remains UNLOADED.

## UNLOAD

UNLOAD destroys the current resident Project:

```text
LOADED
    -> destroy Project
    -> UNLOADED
```

UNLOAD is required before REBUILD.

## Project Identity Persistence

Physical file proof is stored separately from resident Project state.

Current implementation artifact:

```text
<project-dir>/
    .serverengine/
        <project.json filename>/
            project.identity
```

The current artifact is a stepping stone for one configuration file. The final
composed-Project BUILD proof must cover every `project.json` participating in
composition and one aggregate configuration identity.

`project_identity_store` remains a narrow persistence boundary, not a generic
Project persistence manager.

## Configuration Byte Ownership

A configuration file is read once into an owning snapshot:

```text
project_content_snapshot.bytes
    |
    +-- SHA-256
    |
    `-- parser consumes string_view over the same bytes
```

On validation failure ownership moves into diagnostics:

```text
snapshot.bytes
    -> diagnostics.add_source(..., std::move(bytes))
```

No full source-text copy is required.

## Publication and Failure Rule

LOAD and REBUILD start from UNLOADED and publish only after complete success.

BUILD starts from LOADED Gn, constructs a candidate Gn+1, and publishes only
after complete success.

The common failure contract is:

```text
LOAD failure
BUILD failure
REBUILD failure
    -> server_context.project == nullptr
    -> UNLOADED
```

## Architectural Invariants

1. One Server owns zero or one resident Project.
2. `server_context.project == nullptr` exactly means UNLOADED.
3. LOAD and REBUILD require UNLOADED.
4. BUILD and UNLOAD require LOADED.
5. BUILD operates on the currently resident Project; it has no external Project path argument.
6. LOAD, BUILD, and REBUILD remain distinct operations.
7. Failure of LOAD, BUILD, or REBUILD always leaves the Server UNLOADED.
8. Resident `project` contains runtime-required state only.
9. There is no universal `project_context`.
10. Source Manager is construction state, never resident Project state.
11. Path is location, not semantic identity.
12. Publication occurs only after complete operation success.
