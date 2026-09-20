# Project Architecture

## Purpose

Project lifecycle is mode-oriented:

```text
LOAD
BUILD
REBUILD
UNLOAD
```

There is no universal `project_context`.

Each operation owns only the temporary state required by that operation.

The state contract is:

```text
server_context.project == nullptr
    == UNLOADED
```

## Lifecycle

```text
UNLOADED
    +-- LOAD success ------> LOADED Gn
    +-- LOAD failure ------> UNLOADED
    +-- REBUILD success ---> LOADED G0
    `-- REBUILD failure ---> UNLOADED

LOADED Gn
    +-- BUILD success -----> LOADED Gn+1
    +-- BUILD failure -----> UNLOADED
    `-- UNLOAD -----------> UNLOADED
```

Preconditions:

```text
LOAD     requires UNLOADED
REBUILD  requires UNLOADED
BUILD    requires LOADED
UNLOAD   requires LOADED
```

LOAD, BUILD, and REBUILD never substitute for each other.

Failure of LOAD, BUILD, or REBUILD always leaves the Server UNLOADED.

## Resident Project

Target resident state:

```text
project
    +-- Graph
    +-- Runtime
    `-- SHM
```

Resident Project must not retain construction-only state:

```text
project.json composition
configuration manifest
File Context
Parser/frontend
Builder
```

The current resident Project retains its entry path so BUILD can locate the
persisted construction artifacts belonging to Gn.

## LOAD

LOAD restores persisted resident state:

```text
persisted Graph
    -> Runtime
    -> SHM
    -> resident Project Gn
```

LOAD does not:

```text
parse project.json for construction
compose Project configuration
run File Context
perform source change detection
```

LOAD failure leaves UNLOADED.

## REBUILD

REBUILD starts only from UNLOADED and constructs a fresh generation:

```text
root project.json
    -> recursive Project configuration composition
    -> candidate configuration manifest
    -> explicit roots
    -> new File Context
    -> frontend / semantic construction
    -> Graph G0
    -> Runtime
    -> SHM
    -> coordinated persisted-state commit
    -> resident Project G0
```

REBUILD ignores previous incremental construction state.

### Current implementation boundary

The current V4 implementation completes:

```text
root project.json
    -> recursive type:"project" composition
    -> validation of every participating project.json
    -> candidate project_configuration_manifest
    -> aggregate project_configuration_hash
    -> flat File Context population
         project/header/source nodes
         immutable file_id + file_kind
         exact per-file SHA-256 physical state
```

Cardinality is semantic, not generic identity policy:

```text
header  -> repeated use allowed
source  -> duplicate declaration is an error
project -> duplicate reference is an error
           active ancestor reference is a cycle error
```

File IDs are assigned root-first in declaration-order DFS. There is no sort.

It currently stops before:

```text
syntax-specific dependency graph discovery
G0
Runtime
SHM
coordinated commit
```

Because no G0 is published yet, the candidate manifest is deliberately not
persisted by the current incomplete REBUILD path.

## BUILD

BUILD starts only from resident Gn:

```text
resident Gn
    -> load committed project.manifest
    -> verify complete configuration input set
    -> File Context change detection
    -> candidate Gn+1
    -> coordinated commit
    -> resident Gn+1
```

BUILD has no external Project path argument. It uses the active Project.

### Manifest verification

For every manifest entry:

```text
change_token available and proves unchanged
    -> no file read

otherwise
    -> stable snapshot
    -> SHA-256
    -> compare persisted per-file content hash
```

If every participating `project.json` is byte-identical:

```text
configuration unchanged
    -> proceed directly to File Context change detection
```

If any file differs or disappears:

```text
recompose from root
    -> discover added/removed/reordered child Projects
    -> candidate manifest
    -> candidate aggregate configuration hash
```

### Current implementation boundary

The current V4 BUILD implements:

```text
load project.manifest
verify every configuration input
recompose when one input changed
compare aggregate configuration hash
```

It currently stops before File Context and `Gn -> Gn+1` construction.

BUILD failure destroys resident Gn and leaves UNLOADED.

## LOAD Implementation Boundary

LOAD restores a committed Project generation. It does not validate `project.json`
as a substitute for persisted runtime state and must never publish a placeholder
resident Project.

Until committed Graph/Runtime/SHM generation restore exists:

```text
LOAD
    -> project.load_incomplete diagnostic
    -> server_status::unsupported
    -> no resident Project publication
```

Opening or parsing `project.json` is not part of the LOAD proof.

## Configuration Manifest

The complete composed Project configuration proof is represented by:

```text
project_configuration_manifest
    configuration_hash
    files[]
        normalized root-relative path
        SHA-256 content hash
        optional file_change_token
```

Traversal order is:

```text
root-first
declaration-order DFS
platform filesystem case semantics
cycle detection
duplicate Project rejection
NO SORT
```

Repeated references to the same child Project are invalid.

A reference to an active ancestor is a configuration cycle and fails.

### Path contract

Project configuration supports both relative and fully absolute filesystem
locators for `header`, `source`, and `project` items.

Unsupported context-dependent forms are rejected:

```text
Windows drive-relative: C:foo/project.json
rooted but not fully absolute: \foo\project.json
```

The persisted manifest preserves declaration semantics instead of converting all
files to root-relative paths.

```text
project_configuration_file_proof
    declaring_file
    path_type = relative | absolute
    path
    content_hash
    optional change_token
```

`declaring_file` is the index of the `project.json` that declared this child.
Every non-root edge points backward in the root-first declaration-order DFS.
The root uses `invalid_configuration_file`.

Relative locator:

```text
declaring project directory + locator
    -> resolve_project_path(...)
    -> absolute normalized physical path
```

Absolute locator:

```text
locator
    -> resolve_project_path(...)
    -> absolute normalized physical path
```

Resolved physical paths and `project_path_key` values are temporary construction
state and are never persisted in the manifest.

Filesystem equivalence used for identity/cycle/duplicate detection remains isolated behind
`project_path.hpp`:

```text
Windows -> invariant Unicode case-insensitive key
POSIX   -> case-sensitive key
```

Both path resolution and platform-key construction are fail-closed.

Relative locators remain relocation-stable when the composed relative topology is
preserved. Absolute locators are location-bound by definition.

Path/locator identity remains separate from future semantic identity.

## Identity Levels

The identity/proof levels are intentionally separate:

```text
file_change_token
    fast filesystem unchanged proof for one file

file_content_hash
    SHA-256 of exact bytes of one project.json

project_configuration_hash
    SHA-256 of the complete ordered configuration-input manifest

project_semantic_fingerprint
    future canonical semantic identity after semantic composition
```

`project_configuration_hash` includes:

```text
format domain
ordered root-relative paths
ordered per-file content hashes
```

It does not include change tokens.

A whitespace/comment-only change modifies byte identity and therefore the
configuration hash even if a later semantic stage may prove equivalent meaning.

## Manifest Persistence

Committed manifest location:

```text
<root-project-dir>/
    .serverengine/
        <root-project.json filename>/
            project.manifest
```

The file is:

```text
versioned
checksummed
variable-size
fail-closed
```

The manifest checksum protects the artifact bytes.

On load, the aggregate `project_configuration_hash` is also recomputed from the
decoded entries and must match the stored aggregate.

The manifest store is a narrow construction-persistence boundary. It does not
own Graph, File Context, Runtime, SHM, or resident Project state.

## Filesystem Text Boundary

All textual and persisted filesystem paths use strict UTF-8. Internal paths use
the platform-native `std::filesystem::path` representation.

```text
JSON / persisted manifest
    UTF-8
        -> filesystem_path_from_utf8()
        -> native std::filesystem::path

native std::filesystem::path
        -> filesystem_path_to_utf8()
        -> UTF-8 persisted bytes
```

Project-specific path handling begins only after conversion to native form:

```text
UTF-8 locator
    -> native path
    -> resolve_project_path()
    -> make_project_path_key()
```

`project_configuration_hash` hashes the same UTF-8 generic path representation
that the manifest store persists. Locale-dependent narrow path conversion does
not participate in persisted Project identity.

## File Context

`file_context` is the construction-time identity boundary for physical Project
input files.

```text
file_context
    file_id
    canonical filesystem path
    immutable file_kind
```

Syntax routing is explicit:

```text
file_kind::project -> Project configuration syntax
file_kind::header  -> Type syntax
file_kind::source  -> Source syntax
file_kind::assign  -> Assignment/connection syntax
```

The same physical path cannot change kind inside one construction lineage.

The parsers are separate semantic domains:

```text
Type parser
    declarations
    types
    members

Source parser
    declarations
    objects
    links
    initialization

Assign parser
    references existing variables
    emits user connection/assignment facts
    creates no declarations or objects
```

`file_context` contains neither parser state nor parser facts. Dependency discovery is syntax-domain-specific: Project syntax emits project/header/source/assign dependencies, Header syntax discovers Header dependencies, Source syntax uses only Source-language dependency rules, and Assign syntax resolves user connection references.

`file_id` is dense and 1-based. REBUILD creates a fresh identity space. BUILD
restores the previous File Context slots before change detection, preserving every
existing `file_id`; new files append new IDs. IDs are never renumbered or recycled
inside one construction lineage.

File Context storage is compact and allocation-independent per file:

```text
file_record[]          16 bytes / file
native_path_chars[]    one contiguous native-character arena
path_index[]            8 bytes / slot, <= 0.5 load factor
```

At one million files the current path index capacity is 2,097,152 slots, about
16 MiB, while file records consume about 16 MiB. Path storage depends only on the
actual native path characters.

The physical path is retained exactly for I/O. Each dense record keeps only a
32-bit identity fingerprint so hash-table rebuilds never recanonicalize old
paths. A full platform-equivalence `project_path_key` is transient lookup state
and is not stored per file. Matching fingerprints are verified against the exact
platform key, so filesystem identity remains fail-closed.

There is no `file_manager`, no nested `file_update` transaction, no sort, and no
mutex. BUILD and REBUILD construct disposable candidate construction state, so
the whole candidate is the transaction boundary. Failed construction destroys
that candidate.

File Context keeps identity, cold physical state, and dependency topology in
parallel SoA arrays indexed by the same `file_id`:

```text
file_record[]              16 bytes / file
file_physical_record[]     64 bytes / file
file_dependency_record[]   16 bytes / file
native_path_chars[]        one contiguous native-character arena
path_index[]                8 bytes / slot
forward_edges[]             4 bytes / direct edge
reverse_edges[]             4 bytes / direct edge
```

`file_physical_record` contains exact content SHA-256 and the optional native
change token. Filesystem timestamp/size observation is acquisition-local only; it
is not persisted or trusted as an unchanged proof.

Acquisition is split for parallel execution:

```text
prepare_acquire()
    -> borrowed file_acquire_job

execute_acquire()
    -> token proof when available
    -> otherwise stable read + SHA-256

apply_acquire()
    -> update cold physical state
    -> report exact content change
```

`construction_content_hash` is SHA-256 over domain `CWFCNT01`, the ordered file
count, and raw 32-byte per-file SHA-256 values. It intentionally contains no HEX
encoding, path, role, or dependency topology. Those belong to higher construction
identity layers.

`path_hash` is never durable identity. It is rebuilt from physical paths when a
persisted File Context is restored.

Project composition now populates the flat File Context closure for every
explicit `project`, `header`, `source`, and `assign` item. `project.json` uses
the same stable snapshot for manifest proof and File Context physical state.

Reuse policy is intentionally syntax-domain-specific:

```text
header
    reusable declaration input
    multiple incoming uses are valid

source
    one semantic construction unit
    repeated declaration anywhere in the composed Project is invalid

assign
    one user connection-description input
    references existing variables only
    repeated declaration anywhere in the composed Project is invalid

project
    one composed subtree
    repeated reference is invalid
    recursion through an active ancestor is a cycle
```

`file_context` itself owns identity only. These cardinality rules remain in the
Project composition layer.

BUILD recomposition and REBUILD use the same composition semantics before any
File Context-specific work:

```text
same physical path + different file_kind -> error
repeated source                       -> error
repeated assign                       -> error
repeated completed project            -> error
active project ancestor               -> cycle error
repeated header                       -> allowed
```

This keeps configuration acceptance identical whether composition is manifest-only
or also populates a fresh File Context.

## File Dependency Topology

`file_id` is the only identity of a construction-input node.

There is no separate graph-node identity:

```text
file_id
    -> file_record
    -> file_physical_record
    -> file_dependency_record
```

The file itself is the dependency-graph node. V4 therefore does not introduce:

```text
graph_node_id
dependency_id
edge_id
stable_dependency_id
```

A direct dependency relation is only:

```text
file_id -> file_id
```

For every file, construction needs two direct adjacency views:

```text
dependencies(file_id)
dependents(file_id)
```

Only direct relations are stored. Transitive affected sets are discovered by
walking `dependents` from changed files.

Dependency discovery remains syntax-domain-specific:

```text
file_kind::project
    Project configuration syntax
    -> explicit project/header/source/assign inputs

file_kind::header
    Type/Header syntax
    -> Header-language dependencies

file_kind::source
    Source syntax
    -> Source-language dependencies

file_kind::assign
    Assign syntax
    -> references required for user connection validation
```

The topology layer stores only resolved `file_id` relations; it does not contain
paths, parser state, or syntax-specific facts.

### Storage contract

There is one current File Context topology. V4 does not maintain separate `G0`
and `Gn` dependency-storage models.

Committed construction topology is compact:

```text
file_dependency_record[file_id - 1]
forward_edges[]
reverse_edges[]
```

Each range is:

```cpp
struct file_edge_range {
    std::uint32_t offset;
    std::uint32_t count;
};
```

and each dependency record is 16 bytes:

```cpp
struct file_dependency_record {
    file_edge_range dependencies;
    file_edge_range dependents;
};
```

All syntax domains stage temporary `(source file_id, target file_id)` relations
into one File Context arena. Project composition is only the first producer;
Header, Source, and Assign discovery append to the same arena.

After dependency discovery reaches closure, topology is finalized exactly once.
Finalization groups relations by source with a linear counting pass, collapses
duplicate `(source,target)` pairs with dense `file_id` markers, and fills exact
forward/reverse arenas. The algorithm is `O(F + E)` with no sort or hash lookup.
After finalization no new file identity or dependency relation may be added.

Architectural constraints:

```text
NO secondary node identity
NO dependency_id / edge_id
NO SORT
NO per-node heap allocation
NO vector<vector<file_id>>
```

Project composition is the first producer and stages only explicit direct edges:

```text
project -> child project
project -> header
project -> source
project -> assign
```

Other syntax domains add only their own resolved direct dependencies later.

Dependency topology is construction state. It is not resident runtime Project
state after Runtime/SHM construction is complete.

## Publication Contract

Candidate construction artifacts belong to the candidate generation.

They become authoritative only as part of the same successful coordinated commit
that publishes that generation.

Therefore:

```text
failed REBUILD
    must not commit candidate manifest

failed BUILD
    must not commit candidate manifest/baseline
    and destroys resident Gn
```

## Architectural Invariants

1. One Server owns zero or one resident Project.
2. `server_context.project == nullptr` exactly means UNLOADED.
3. LOAD and REBUILD require UNLOADED.
4. BUILD and UNLOAD require LOADED.
5. BUILD operates on the one current resident Project and creates disposable candidate construction state.
6. LOAD, BUILD, and REBUILD are distinct pipelines.
7. Failure of LOAD, BUILD, or REBUILD leaves UNLOADED.
8. Resident Project contains runtime-required state only.
9. There is no universal `project_context`.
10. File Context is construction state, never resident Project state.
11. One normalized configuration path appears at most once in the manifest.
12. Configuration composition is declaration-order DFS and is never sorted.
13. Change tokens are proof optimizations, never identity.
14. Per-file SHA-256 identifies exact configuration bytes.
15. Aggregate configuration hash identifies the complete ordered configuration input set.
16. Relative-locator configuration identity is relocation-stable while composed relative topology is preserved; absolute locators are location-bound.
17. Semantic fingerprint remains separate from byte/configuration identity.
18. Candidate persisted construction state is committed only with its successful generation.
19. Project absolute-path resolution must fail closed; unresolved paths must never become construction identities.
20. `file_id` is the only identity of a file-dependency node; no secondary graph-node ID exists.
21. File dependency storage contains direct `file_id -> file_id` relations only.
22. Forward and reverse adjacency are first-class construction data.
23. File dependency records are indexed directly by `file_id - 1`.
24. Dependency topology uses one compact forward/reverse representation; there is no separate G0/Gn storage model.
