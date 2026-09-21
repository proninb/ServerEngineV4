# Server Architecture V4

## Physical source tree

Visual Studio must mirror the physical file-system hierarchy exactly.

Current Project construction subtrees include:

```text
server_engine/
├── project/
│   ├── assign/
│   ├── construction/
│   ├── file/
│   ├── frontend/
│   ├── preprocessor/
│   ├── persistence/
│   ├── semantic/
│   ├── string/
│   ├── project.*
│   ├── project_build.*
│   ├── project_load.*
│   ├── project_rebuild.*
│   ├── project_configuration_*
│   ├── project_identity.hpp
│   ├── project_lifecycle_context.hpp
│   ├── project_path.*
│   └── preprocessor_configuration.hpp
└── docs/
    ├── SERVER_ARCHITECTURE.md
    ├── SERVER_CONFIGURATION.md
    ├── PROJECT.md
    ├── PROJECT_CONFIGURATION.md
    ├── FRONTEND_ARCHITECTURE.md
    ├── DIAGNOSTICS.md
    └── JSON.md
```

As new Project subtrees are introduced, the physical directory is the
architecture; IDE filters mirror it rather than inventing logical-only groups.

## Visual Studio rule

`.vcxproj.filters` must reproduce the physical source tree exactly.

No artificial `Source Files`, `Header Files`, `Server`, or other logical-only
groups are used.

Current Project filters must include the physical:

```text
project
├── assign
├── construction
├── file
├── frontend
├── persistence
├── preprocessor
├── semantic
└── string
```

The physical tree is authoritative.

## Server ownership

```text
main
 |
 +-- server
      |
      +-- server_context
           |
           +-- server_configuration
           |    `-- one process-wide ABI
           +-- command_queue
           +-- communication
           |    |
           |    +-- console endpoint, only when configured
           |
           +-- project
```

## Server ABI and SHM contract

One Server instance owns one ABI and one SHM layout contract.

```text
server.json
    -> settings.abi.target
    -> settings.abi.pack

one Server
    -> one ABI
    -> one SHM
```

ABI is process configuration. `project.json` does not contain or override ABI.

Each mode-specific operation borrows the same process-wide
`server_settings_configuration`. The explicit root Project path is an operation
input; it is not resident Project state and is not stored in `server_context`.

```text
LOAD
    root Project path
    load_context
        Server settings

BUILD
    root Project path
    build_context
        Server settings
        committed baseline views
        temporary BUILD reuse/change state

REBUILD
    root Project path
    rebuild_context
        Server settings
        fresh construction state
```

There is no universal `builder_context`.

## Configuration directory

The directory name is `configuration`, not `config`.

It contains only Server process configuration types and loading/parsing logic.

## Communication directory

`communication/` owns common transport-neutral communication contracts:

```text
server_command
command_queue
communication
```

Transport-specific implementation belongs in subdirectories.

Current transport-specific subtree:

```text
communication/
└── console/
```

Future examples may include:

```text
communication/
├── console/
├── tcp/
└── ...
```

without moving transport-neutral command infrastructure.

## Console cross-platform implementation

The console transport is split into:

```text
console_input.hpp
console_input_windows.cpp
console_input_posix.cpp
server_console.hpp
server_console.cpp
```

`console_input.hpp` is platform-neutral.

Windows implementation:

```text
WaitForMultipleObjects(
    STD_INPUT_HANDLE,
    stop_event
)
```

POSIX implementation:

```text
poll(
    STDIN_FILENO,
    wake_pipe
)
```

Only the implementation file for the active platform is compiled by CMake.

The native Visual Studio project includes only the Windows backend because that
project is a Windows build artifact.

## Optional console

If no console endpoint is present in `server.json`:

- no `server_console` object is created;
- no console thread is started;
- no console input backend is opened.

Console is one communication transport, not a mandatory Server component.

## Architectural invariants

1. Physical folders define architecture.
2. Visual Studio mirrors physical folders exactly.
3. `configuration/` contains process configuration contracts/loaders.
4. `communication/` contains shared command infrastructure.
5. Each transport owns its dedicated implementation subtree.
6. Server/control thread is the sole lifecycle owner.
7. At most one resident Project is active.
8. `server_context.project == nullptr` exactly means `UNLOADED`.
9. LOAD, BUILD, and REBUILD require `UNLOADED`.
10. UNLOAD requires `LOADED`.
11. Resident Project contains runtime state only.
12. BUILD acceleration state belongs to the persisted baseline, not `server_context`.
13. BUILD/REBUILD publish only after one successful coordinated commit.
14. Runtime hot paths do not depend on control-plane synchronization.
15. V4 has one Graph concept: `G`. BUILD/REBUILD do not create Graph generations.
16. A/B slots and baseline switching are persistence mechanics, not Graph state.

## PIMPL construction rule

`console_input::implementation` is intentionally private and platform-specific.

Because `std::unique_ptr<implementation>` requires a complete implementation type
when constructing the object, `console_input` constructor/destructor definitions
live in each platform implementation file after the corresponding
`console_input::implementation` definition.

There is no shared `console_input_common.cpp`.

This avoids creating or destroying an incomplete PIMPL type and keeps native
Windows/POSIX state fully isolated inside its backend.


## Command execution result

```text
communication endpoint
        |
        v
server_command_request
|- server_command
`- direct origin callback
        |
        v
command_queue
        |
        v
Server/control thread
        |
        v
server::execute()
        |
        v
server_command_result
|- operation_id
|- server_status
`- diagnostic_collection
        |
        v
direct origin callback
        |
        v
originating endpoint
```

Invariants:

1. Server/control thread is the sole lifecycle owner.
2. `server::execute()` performs lifecycle behavior only and does not format or print.
3. One external command creates exactly one `operation_id` and one `diagnostic_collection`.
4. `server_command_result` owns the complete operation result.
5. `server_command_request::origin` is direct and non-owning; no endpoint lookup is performed.
6. No virtual response hierarchy or shared ownership is used.
7. SHUTDOWN result is presented before communication endpoints are stopped.
8. Failed LOAD, BUILD, or REBUILD leaves Server UNLOADED.

## Project LOAD entry points

There are exactly two ways to request LOAD:

```text
1. Startup auto-LOAD

server.json
    |
    `- project.path
          |
          v
      server::load()

2. Runtime command

LOAD <project.json>
          |
          v
      server::load()
```

Both paths use the same `server::load()` implementation.

`server::load()` is the single owner of:

- UNLOADED/LOADED validation;
- project path resolution relative to `server.json`;
- Project construction;
- Project LOAD execution;
- failed-LOAD cleanup.

The state contract is:

```text
context.project == nullptr
    == UNLOADED
```

LOAD is valid only while UNLOADED.

```text
UNLOADED + LOAD success -> LOADED
UNLOADED + LOAD failure -> UNLOADED
LOADED   + LOAD         -> project_already_loaded
```

Startup `project.path` with `startup=load` is not a separate loading mechanism
and does not bypass normal LOAD state validation.

## Project documentation

Project construction is mode-oriented. There is no universal Project
construction context.

Temporary operation state is owned by explicit `load_context`, `build_context`,
and `rebuild_context` boundaries.

Detailed contracts are separated into:

```text
PROJECT.md
    lifecycle
    persisted baseline
    SourceSave / DB / compiled-G boundaries
    BUILD / REBUILD / LOAD publication

PROJECT_CONFIGURATION.md
    project.json schema
    composition
    configuration proof

FRONTEND_ARCHITECTURE.md
    file_id / string_id / identity_ref
    preprocessing/frontend
    lexical reuse
    dependency topology
```

Server architecture owns process lifecycle and resident Project publication.

## Persisted Project baseline

The Project artifact root remains:

```text
<root-project-dir>/.serverengine/<root-project.json filename>/
```

One successful BUILD/REBUILD baseline logically contains files whose standard
names come from process-wide `server.json.settings.files`:

```text
configuration proof
    settings.files.manifest

SourceSave
    physical file identity/state
    forward/reverse file topology

DB
    BUILD-only lineage and acceleration state
    retained lexical state
    string_id and identity_ref continuity

compiled G
    the one compiled Project result used by LOAD/Runtime
    may also carry derived ABI-layout acceleration keyed by exact {target, pack}
```

The artifact roles and filenames are configured by `settings.files.manifest`,
`settings.files.source_save`, `settings.files.database`, `settings.files.compiled`,
and `settings.files.baseline`. Binary formats remain versioned persistence contracts.

The existing `project_configuration_manifest_store` remains a narrow codec/store
for the configuration-proof component. Its standalone replacement operation is
not the final authoritative multi-artifact commit once the full baseline exists.

The persistence boundary now defines the final A/B physical layout:

```text
.serverengine/<root-project.json>/
    baseline.bin
    slot0/
        project.manifest
        source.bin
        database.bin
        compiled.bin
    slot1/
        project.manifest
        source.bin
        database.bin
        compiled.bin
```

`slot0` and `slot1` are crash-safe replacement mechanics only. They do not
represent Graph versions, generations, or runtime state.

`source.bin` has a versioned/checksummed image contract for finalized File
Context state. It records file_id order, UTF-8 physical paths, file kind,
content/change proof, and direct forward/reverse topology. The encoder refuses
a File Context whose dependency topology is not terminally finalized.

`database.bin` has a sectioned versioned/checksummed base image. Current sections
persist dense `string_id` spelling order, retained lexical state in canonical
`file_id` order, and semantic identity lineage in canonical `identity_ref` slot
order. CPU-lane arenas and semantic lookup indexes are construction-only and
never persisted.

`database.bin` is BUILD acceleration/lineage state. It is not a Semantic DB, is
not an intermediate representation of the Project, and is not input to a
Builder stage. Additional sections are justified only when they let BUILD avoid
repeating work while preserving the same one-G construction model.

Persisted ABI layout, when present, is derived acceleration beside `G`.
It is reusable only when its exact `abi_layout_key {target, pack}` matches the
current process-wide Server ABI. An ABI mismatch requires layout recomputation
from `G`; it does not create another Graph and does not move ABI state into
`database.bin`.

Persisted ABI layout, when present, is derived acceleration beside `G`.
It is reusable only when its exact `abi_layout_key {target, pack}` matches the
current process-wide Server ABI. An ABI mismatch requires layout recomputation
from `G`; it does not create another Graph and does not move ABI state into
`database.bin`.

There is no SAVE lifecycle command.

Successful BUILD/REBUILD owns persistence. Failed construction must leave the
last successful baseline intact.

## Project configuration locator graph

The configuration manifest preserves how each child Project was declared.

```text
entry N
    declaring_file -> earlier entry
    locator_type    -> relative | absolute
    locator         -> normalized declared filesystem locator
```

Root-first declaration-order DFS guarantees:

```text
declaring_file < N
```

for every non-root entry. BUILD therefore reconstructs physical configuration
paths in one linear pass using already resolved parent entries.

The manifest store validates the distinguished root entry on both write and
read:

```text
declaring_file = invalid_configuration_file
locator_type = relative
locator = root project.json filename
```

Invalid root metadata is rejected by the artifact boundary itself.

```text
NO SORT
NO LOOKUP
```

Relative locators resolve from the declaring Project directory. Absolute locators
resolve directly and are location-bound. Resolved paths and platform path keys
remain temporary construction state.

## Project lifecycle state machine

```text
UNLOADED
    +-- LOAD <path> success ------> LOADED
    +-- LOAD <path> failure ------> UNLOADED
    +-- BUILD <path> success -----> LOADED
    +-- BUILD <path> failure -----> UNLOADED
    +-- REBUILD <path> success ---> LOADED
    `-- REBUILD <path> failure ---> UNLOADED

LOADED
    `-- UNLOAD -------------------> UNLOADED
```

LOAD, BUILD, and REBUILD each receive a Project path.

BUILD opens the last successful persisted baseline for that Project path. It
does not consume `server_context.project`.

If BUILD fails, the old persisted baseline remains available for later
incremental reuse, but no resident Project remains active.

## Current Project construction boundary

The implemented REBUILD path currently reaches:

```text
recursive project.json composition
    -> root preprocessor configuration
    -> configuration manifest
    -> fresh File Context
    -> Header/Source exact-byte materialization
    -> retained lexical generation
    -> sparse directive anchors
    -> deterministic executed quoted-include closure
    -> Assign exact-byte materialization
```

It stops before Parser/Semantic construction of G, Assign resolution,
terminal dependency-topology finalization, compiled-G persistence, and the
coordinated commit.

The implemented BUILD code currently reaches only:

```text
project.manifest load
    -> configuration-input verification
    -> recomposition when configuration bytes changed
    -> aggregate hash comparison
```

The C++ BUILD entry now matches the lifecycle contract:

```text
UNLOADED
    -> BUILD <project-path>
    -> resolve Project path relative to server.json
    -> open committed baseline
```

BUILD does not consume resident Project state. Remaining BUILD work includes
SourceSave/File Context change detection, affected reverse closure, DB reuse,
affected frontend/Parser/Semantic work, construction of G, and coordinated
persistence.

## Filesystem and Project path boundaries

Filesystem path concerns are split by ownership.

The common boundary:

```text
filesystem_path.hpp
filesystem_path.cpp
```

owns:

```text
strict UTF-8 <-> native path conversion
filesystem_path_key
filesystem_path_key_hash
make_filesystem_path_key()
```

Filesystem-equivalence semantics are platform-defined inside that common
boundary:

```text
Windows
    lexically normalized path
        -> invariant Unicode lowercase key
        -> case-insensitive equivalence

POSIX
    lexically normalized path
        -> case-sensitive equivalence
```

This contract is shared by Server configuration validation and Project
construction. `configuration/` does not depend on `project/`.

The Project-specific boundary:

```text
project_path.hpp
project_path.cpp
```

owns only:

```text
resolve_project_path(path, output)
    -> absolute
    -> lexically normalized
    -> success | failed
```

Project construction therefore uses the two boundaries in sequence:

```text
input locator
    -> resolve_project_path()
    -> make_filesystem_path_key()
```

Both operations are status-returning no-exception boundaries. Failure stops
construction; there is no fallback to different filesystem-equivalence
semantics.

## File Context storage boundary

`file_context` is construction state, never resident Project runtime state.

Identity lifetime:

```text
REBUILD
    fresh file_id space

BUILD
    bind/restore committed slots
    preserve existing file_id
    append new file_id values only
```

Logical compact state:

```text
file_record[]
file_physical_record[]
file_dependency_record[]
native_path_chars[]
path_index[]
forward_edges[]
reverse_edges[]
```

The current implementation materializes these arrays for a fresh construction.

BUILD will add committed-baseline views plus sparse mutable overlays. It must not
copy or rebuild all file state merely to change a sparse subset.

Physical acquisition retains the existing fast proof contract:

```text
native change token proves unchanged
    -> no read

otherwise
    -> stable exact read
    -> SHA-256
```

The direct reverse topology is persisted SourceSave data because BUILD uses it
to compute the affected closure before updating dependency relations for the
current construction.

REBUILD may build complete topology in `O(F + E)` with the current no-sort
finalizer.

BUILD must update topology sparsely while preserving the same logical
`file_id -> file_id` adjacency model.

There is no generation-specific dependency-node identity and no Graph-generation
model. BUILD may reuse persisted storage internally, but the architectural
result of LOAD, BUILD, or REBUILD is always one `G`.

