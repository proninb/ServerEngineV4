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
│   ├── graph/
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
        persisted BUILD-state views
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
12. BUILD acceleration state is persisted outside resident `project`.
13. LOAD requires only the compiled Project artifact.
14. Missing required BUILD acceleration state means REBUILD is required.
15. Runtime hot paths do not depend on control-plane synchronization.
16. V4 has one Graph concept: `G`. BUILD/REBUILD do not create Graph generations.

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
    persisted BUILD state
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

## Persisted Project artifacts

The Project artifact root is:

```text
<root-project-dir>/.serverengine/<root-project.json filename>/
```

The direct physical layout is:

```text
.serverengine/<root-project.json>/
    project.manifest
    source.bin
    database.bin
    compiled.bin
```

The filenames come from `server.json.settings.files`.

Artifact roles are intentionally asymmetric:

```text
LOAD
    compiled.bin

BUILD
    project.manifest
    source.bin
    database.bin
    compiled.bin

REBUILD
    constructs fresh state
    replaces the produced artifacts
```

`compiled.bin` contains the one compiled Project result used by LOAD to obtain
G. It is mmap-native: semantic strings/identities, G arrays, persisted read
indexes, the physical-file/root Source Map, and the ordered Studio-facing Assign
table are bound directly from the mapped file without reconstructing mutable containers. ABI-derived Runtime
layout belongs to Phase 2 and is not part of the current compiled format.

`project.manifest`, `source.bin`, and `database.bin` are BUILD
acceleration/lineage state. LOAD does not open them.

If a required BUILD artifact is absent or invalid, incremental BUILD cannot
continue and REBUILD is required.

There is no selector file or active/inactive persistence slot.

`source.bin` has a versioned/checksummed image contract for finalized File
Context state plus BUILD-only semantic presence sidecars. BUILD memory-maps it
read-only directly.
`source_save_view::bind()` is O(1) and allocation-free. `source.bin` v4 also
contains an mmap-native normalized-path lookup index used by sparse BUILD. The
index is encoded directly into the final mapping with no full transient index
copy; BUILD does not rebuild an O(F) path hash table before include discovery.

BUILD change discovery is journal-first. Before dirty detection, BUILD captures
the checkpoint for the `source.bin` that may be produced by that BUILD. Dirty
detection starts from the persisted SourceSave checkpoint.

A valid Windows/NTFS SourceSave checkpoint reads the volume USN journal once and
maps changed file references through the persisted `file_reference -> file_id`
index. Matching data events are rebuilt directly. A dense bitset emits ascending
`file_id` order without sorting or per-file opens.

If journal continuity/support is unavailable, BUILD selects every current
persisted `file_id` as an acquisition candidate without filesystem reads.
A shared parallel exact-acquisition stage then reads/hashes each candidate once;
only SHA-256-different or missing files enter sparse File Context overlays.
Persisted OLD reverse topology expands `semantic_changed`, not raw journal
events, into the affected closure.

`database.bin` is BUILD-only retained frontend state keyed by `file_id`. It
pairs exact Header/Source snapshot bytes with their lexical words/directive
anchors. Exact changed Header/Source records are masked before parallel lexical
replacement starts; present changed files tokenize from the bytes retained by
exact classification, while missing files remain masked until affected
preprocessing decides whether they are still referenced. It is not a Semantic DB
and does not persist String Table or Identity Space state.
`compiled.bin` is the sole persisted owner of `string_id` / `identity_ref`
lineage as well as G.

ABI-derived Runtime layout is deferred to Phase 2. Its representation and any
future persistence/reuse policy are not part of the current Phase-1 artifact
contract.

There is no SAVE lifecycle command.

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

### Source provenance boundary

Resident Project source provenance is final compiled metadata, not Graph hot
state. Runtime/Studio will obtain `physical file -> semantic data` from the
`compiled.bin` Source Map. BUILD-only physical dependency topology remains in
`source.bin`; aggregate semantic presence will live beside that topology rather
than inside G or inside individual DAG nodes.

Construction provenance is produced directly by Parser/Semantic and persisted
mmap-native in `compiled.bin`. BUILD-only semantic presence is persisted beside
the physical topology in `source.bin`.

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

BUILD opens the persisted BUILD artifacts for that Project path. It
does not consume `server_context.project`.

If BUILD fails, the old persisted BUILD state remains available for later
incremental reuse, but no resident Project remains active.

## Compiled Source Provenance Boundary

The final Project keeps G free of file ownership. Source provenance is a cold
compiled sidecar intended for Runtime/Studio inspection and sparse BUILD
reconciliation.

```text
G
    semantic/runtime result

Source Map
    semantic root -> contributions
    physical file -> contribution indices
```

Source Map is not a semantic dependency graph and does not alter the File
Context dependency DAG.


## Development phase boundary

Current implementation work is deliberately ordered as:

```text
PHASE 1
LOAD / BUILD / REBUILD
    -> G

PHASE 2
G
    -> Runtime
    -> SHM
    -> resident Project
```

Phase 1 includes the persistence required for the three ways of obtaining G.
REBUILD uses direct final artifact paths and deletes the full artifact set on
failure. BUILD has a different failure contract and therefore does not inherit
REBUILD persistence mechanics automatically.

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
    -> Assign exact-byte materialization
    -> ordered Assign user-table construction
    -> one Parser/Semantic preprocessing execution
         -> active quoted-include discovery
         -> direct G construction
    -> terminal dependency-topology finalization
```

Assign is Studio-facing user data only. It performs no semantic variable
resolution, G mutation, Runtime binding, or file dependency emission.

REBUILD starts a fresh persisted lineage by removing
`project.manifest`, `source.bin`, `database.bin`, and `compiled.bin`. It uses
the final artifact names directly; there are no `.tmp` files, A/B slots,
selector files, or rollback generations. Any REBUILD failure closes
operation-local mappings before removing all four artifacts again. Cleanup I/O
failure is reported explicitly and leaves the Server `UNLOADED`.

After topology finalization, the current REBUILD implementation persists
`project.manifest`, `source.bin`, `database.bin`, and `compiled.bin` directly to
their final paths using exact-size writable mappings. source.bin is encoded
directly from finalized File Context state. database.bin streams exact
Header/Source snapshot bytes together with retained Lexical Generation records,
words, and directive anchors into mapped sections; String Table and Identity
Space lineage are persisted once in compiled.bin.
The optional SourceSave USN identity indexes are prepared once and retained only
as persistence-specific tracking state. None of these production paths creates
a full-size serialized `std::vector<std::byte>` or `.tmp` artifact. source.bin
and database.bin receive cold verification before flush;
`compiled_project_view::bind()` plus cold `verify_contents()` validates the
compiled mapped bytes.

LOAD maps and structurally binds an existing `compiled.bin` read-only without
rebuilding Graph/string/identity containers. The mapping and
`compiled_project_view` are owned by the resident `project`; `verify_contents()`
remains a separate cold audit and is not part of the normal LOAD hot path.

REBUILD publishes through the same resident representation only after all four
artifacts are validated, flushed, and their writable construction mappings are
closed. A publication failure is still a REBUILD failure and removes the full
artifact set. LOAD and REBUILD therefore complete their Phase-1 `-> G` contract;
BUILD remains the unfinished Phase-1 lifecycle path before Runtime/SHM work.

The implemented BUILD code currently reaches:

```text
project.manifest load
    -> configuration-input verification
    -> recomposition when configuration bytes changed
    -> aggregate hash comparison
    -> source.bin read-only mmap/bind
    -> exact physical dirty detection
    -> OLD reverse dependency affected closure
```

The C++ BUILD entry now matches the lifecycle contract:

```text
UNLOADED
    -> BUILD <project-path>
    -> resolve Project path relative to server.json
    -> open persisted BUILD artifacts
```

BUILD does not consume resident Project state. Remaining BUILD work includes
persisted File Context/DB reuse, sparse affected frontend/Parser/Semantic work,
and construction of G. BUILD persistence mechanics are intentionally not frozen
yet because a failed BUILD must preserve the previously persisted BUILD state.

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

REBUILD materializes these arrays for a fresh construction.

BUILD now binds `source.bin` directly as the committed File Context baseline.
Existing file identity and adjacency remain mmap-backed; only touched existing
files materialize sparse native-path/physical/content state, while newly
discovered files append local records after the committed `file_id` range.
No O(F) File Context reconstruction is performed at BUILD startup.

Affected dependency adjacency is now replaced sparsely. BUILD marks each source
whose outgoing set is reconstructed, compares only that source's old mmap edges
with its new staged edges, and keeps reverse changes as sparse add/remove deltas.
Unchanged adjacency remains mmap-backed; no dense topology reconstruction occurs.

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

`database.bin` is bound separately as the immutable source/lexical BUILD
baseline. File Context reads unchanged Header/Source spelling directly from mmap
through `file_content_baseline_view`; `lexical_generation` reads encoded
words/directive anchors through `lexical_baseline_view`. Changed/appended files
occupy sparse native overlays. Neither construction subsystem depends on
database persistence implementation types.

REBUILD may build complete topology in `O(F + E)` with the current no-sort
finalizer.

BUILD updates topology in `O(A + E_old(A) + E_new(A))` for `A` replaced
sources while preserving the same logical `file_id -> file_id` adjacency model.
This bound deliberately includes reading old outgoing edges: exact removals
cannot be known without examining the previous adjacency of a replaced source.

There is no generation-specific dependency-node identity and no Graph-generation
model. BUILD may reuse persisted storage internally, but the architectural
result of LOAD, BUILD, or REBUILD is always one `G`.


## Semantic Source Map

V4 keeps semantic source provenance separate from hot G records.

```text
G
    final semantic WHAT
    no file_id/source_id in type/object/link records

compiled.bin Source Map
    semantic root -> contiguous contributions
    physical file_id -> secondary contribution index

source.bin
    physical file state
    direct preprocessing/file dependency DAG
    BUILD-only semantic presence counters
```

One canonical Source Map contribution is:

```text
{ physical file_id, semantic data }
```

The semantic data is one of:

```text
type declaration -> identity_ref slot
type definition  -> identity_ref slot
object           -> identity_ref slot
link             -> link_handle slot
```

Members are not duplicated in Source Map. A type definition maps to G and its
members are queried from that type. Object type and link endpoints are likewise
queried from G.

The same physical header may execute under more than one semantic root. Those
executions are distinct ownership facts even when they produce the same physical
datum. Canonical storage is therefore:

```text
root_file_id -> contiguous contribution range
```

There is no global `(physical file_id, semantic datum)` canonicalization and no
root contribution-index array. Runtime/Studio's physical query is the one
secondary index:

```text
file_id -> contribution_id[]
```

Both views refer to the same root-owned contribution records; semantic payload is
not copied into a second representation.

The File Context DAG remains only direct file dependency topology produced by
actual preprocessing execution. Source Map ownership is not a second semantic
dependency DAG.

`source.bin` persists aggregate semantic presence needed to subtract/add root
ownership during sparse BUILD:

```text
type_handle   -> declaration_count, definition_count
object_handle -> producer_count
link_handle   -> producer_count
```

These counters are derived from root-owned contributions and are BUILD state.
They are not Runtime semantic payload and are not embedded in G.
