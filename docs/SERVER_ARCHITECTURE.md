# Server Architecture V4

## Physical source tree

Visual Studio must mirror the physical file-system hierarchy exactly.

```text
ServerEngineV4/
├── server.json
├── server_engine/
│   ├── main.cpp
│   ├── server.hpp
│   ├── server.cpp
│   ├── server_context.hpp
│   ├── server_status.hpp
│   │
│   ├── diagnostics/
│   │   ├── diagnostic.hpp
│   │   ├── diagnostic_collection.hpp
│   │   ├── diagnostic_collection.cpp
│   │   ├── diagnostic_builder.hpp
│   │   ├── diagnostic_source_cache.hpp
│   │   ├── diagnostic_source_cache.cpp
│   │   ├── diagnostic_formatter.hpp
│   │   ├── diagnostic_formatter.cpp
│   │   ├── diagnostic_descriptor.hpp
│   │   └── diagnostic_registry.hpp
│   │
│   ├── json/
│   │   ├── json.hpp
│   │   ├── json_ascii.hpp
│   │   ├── json_unicode.hpp
│   │   ├── json_value.hpp
│   │   ├── json_parser.hpp
│   │   ├── json_parser.cpp
│   │   ├── json_buffer.hpp
│   │   ├── json_escape.hpp
│   │   └── json_writer.hpp
│   │
│   ├── configuration/
│   │   ├── server_configuration.hpp
│   │   ├── server_configuration_loader.hpp
│   │   └── server_configuration_loader.cpp
│   │
│   ├── communication/
│   │   ├── server_command.hpp
│   │   ├── server_command_request.hpp
│   │   ├── server_command_result.hpp
│   │   ├── command_queue.hpp
│   │   ├── command_queue.cpp
│   │   ├── communication.hpp
│   │   ├── communication.cpp
│   │   │
│   │   └── console/
│   │       ├── console_input.hpp
│   │       ├── console_input_windows.cpp
│   │       ├── console_input_posix.cpp
│   │       ├── server_console.hpp
│   │       └── server_console.cpp
│   │
│   └── project/
│       ├── project.hpp
│       ├── project.cpp
│       ├── project_identity.hpp
│       ├── project_lifecycle_context.hpp
│       ├── project_preprocessor_configuration.hpp
│       ├── project_configuration_manifest.hpp
│       ├── project_configuration_manifest.cpp
│       ├── project_configuration_manifest_store.hpp
│       ├── project_configuration_manifest_store.cpp
│       ├── project_path.cpp
│       ├── project_path.hpp
│       ├── project_path_windows.cpp
│       ├── project_path_posix.cpp
│       ├── file/
│       │   ├── file_identity.hpp
│       │   ├── file_identity.cpp
│       │   ├── file_context.hpp
│       │   └── file_context.cpp
│       ├── project_load.hpp
│       ├── project_load.cpp
│       ├── project_build.hpp
│       ├── project_build.cpp
│       ├── project_rebuild.hpp
│       ├── project_rebuild.cpp
│       ├── project_configuration_loader.hpp
│       └── project_configuration_loader.cpp
│
└── docs/
    ├── SERVER_ARCHITECTURE.md
    ├── SERVER_CONFIGURATION.md
    ├── PROJECT.md
    ├── PROJECT_CONFIGURATION.md
    ├── DIAGNOSTICS.md
    └── JSON.md
```

## Visual Studio rule

`.vcxproj.filters` must reproduce the physical folder tree:

```text
server_engine
├── configuration
├── communication
│   └── console
└── project
    └── file

docs
```

No artificial `Source Files`, `Header Files`, `Server`, or other logical-only
groups are used.

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
    -> abi.target
    -> abi.pack

one Server
    -> one ABI
    -> one SHM
    -> all Projects/subprojects use the same physical layout contract
```

ABI is process configuration. `project.json` does not contain or override ABI.

The same parsed `server_abi_configuration` is passed into the mode-specific
temporary lifecycle context:

```text
LOAD
    load_context
        ABI

BUILD
    build_context
        ABI
        persisted/candidate construction state

REBUILD
    rebuild_context
        ABI
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
3. `configuration/` contains configuration contracts and loaders.
4. `communication/` contains shared command infrastructure.
5. Each transport owns a dedicated communication subfolder.
6. Platform-specific code remains inside the transport implementation subtree.
7. Server lifecycle behavior remains transport-neutral.
8. At most one Project is active.
9. Project state is published only after the selected lifecycle operation succeeds.
10. Runtime hot paths must not depend on control-plane command synchronization.


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

Project construction is mode-oriented. There is no universal Project construction context.

Temporary construction state is owned by explicit `load_context`, `build_context`, and `rebuild_context` boundaries.

Server architecture owns process lifecycle and the resident Project pointer.

Detailed Project contracts are separated into:

```text
PROJECT.md
    Project ownership
    resident vs construction lifetime
    LOAD / BUILD / REBUILD
    separate LOAD / BUILD / REBUILD pipelines
    Graph -> Runtime -> SHM publication

PROJECT_CONFIGURATION.md
    project.json schema
    Project tree
    group / header / source / project
    path resolution
    composition input
    per-project preprocessing configuration
```

Project lifecycle/configuration details belong in those documents instead of
being duplicated in Server process configuration documentation.


## Project configuration manifest persistence

The complete composed configuration proof is stored under:

```text
<root-project-dir>/.serverengine/<root-project.json filename>/project.manifest
```

The manifest stores:

```text
root-first declaration-order composition
declaring-file edges
relative/absolute locator semantics
per-file SHA-256
optional file change tokens
aggregate project_configuration_hash
artifact checksum
```

The manifest is construction state and never resident Project state.

`project_configuration_manifest_store` is a narrow persistence boundary, not a
generic Project persistence manager.

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
    +-- LOAD success ------> LOADED
    +-- LOAD failure ------> UNLOADED
    +-- REBUILD success ---> LOADED
    `-- REBUILD failure ---> UNLOADED

LOADED
    +-- BUILD success -----> LOADED
    +-- BUILD failure -----> UNLOADED
    `-- UNLOAD -----------> UNLOADED
```

BUILD operates on the resident Project and therefore has no Project path
argument. REBUILD requires UNLOADED. Failure of LOAD, BUILD, or REBUILD always
leaves `server_context.project == nullptr`.

## Current Project construction boundary

The current Project construction layer implements configuration composition and
the initial lexical construction boundary:

```text
REBUILD
    recursive project.json composition
    -> local preprocessor configuration per project.json
    -> candidate manifest
    -> flat File Context
    -> Header/Source lexical generation
    -> stops before directive execution / Semantic / G0

BUILD
    committed manifest verification
    -> recomposition on changed configuration bytes
    -> local preprocessor configuration per project.json
    -> aggregate hash comparison
    -> stops before Gn->Gn+1 source construction
```

A candidate manifest is not committed until the generation it describes is
successfully constructed and published.

Configuration traversal is root-first declaration-order DFS with normalized-path
dedupe and cycle detection. It is not sorted.

## Project path platform boundary

Project composition is platform-neutral.

Filesystem path processing is isolated behind:

```text
project_path.hpp
project_path.cpp
project_path_windows.cpp
project_path_posix.cpp
```

The generic composition layer uses:

```text
resolve_project_path(path, output) -> success | failed
make_project_path_key(path, output) -> success | failed
```

`project_path.cpp` owns portable absolute-path resolution and lexical
normalization.

Windows:

```text
lexically normalized path
    -> invariant Unicode lowercase key
    -> case-insensitive dedupe/cycle detection
```

POSIX:

```text
lexically normalized path
    -> case-sensitive dedupe/cycle detection
```

`project_path_windows.cpp` and `project_path_posix.cpp` own only
filesystem-equivalence key construction. The generic manifest layer contains no
Windows/POSIX API code.

Both path operations are status-returning no-exception boundaries. Failure at
either boundary stops Project composition. There is no unresolved-path fallback
and Windows never falls back to case-sensitive semantics.

CMake selects exactly one platform key implementation. Each platform `.cpp`
contains a compile-time fail-closed guard so an incorrect build selection cannot
silently produce an empty or wrong translation unit.

## File Context storage boundary

`file_context` is temporary Project construction state.

```text
REBUILD
    fresh File Context identity space

BUILD
    restore committed File Context slots
    preserve existing file_id values
    append IDs only for newly discovered files
```

The physical path is stored once in one contiguous native-character arena.
Platform-equivalence keys are transient lookup values and never replace the
physical I/O path.

```text
file_record[]       dense file_id slots
native_path_chars[] physical paths
path_index[]        open-addressed path identity index
```

There is no per-file `std::filesystem::path` allocation and no persisted
`project_path_key` object in File Context. No sort or mutex is required.

Physical acquisition is owned by `file/file_identity.*`, not Project semantic
identity. The cold physical record stores exact SHA-256 plus an optional native
change token; timestamp/size observation remains transient.

`construction_content_hash` aggregates ordered raw per-file SHA-256 values under
domain `CWFCNT01`. It proves aggregate byte-content identity only; file path,
role, configuration, and future dependency topology remain separate inputs to a
higher construction identity.
