# Project Architecture

## Purpose

Project lifecycle is mode-oriented:

```text
LOAD <project-path>
BUILD <project-path>
REBUILD <project-path>
UNLOAD
```

There is no universal `project_context`.

Each operation owns only the temporary state required by that operation.

The state contract remains:

```text
server_context.project == nullptr
    == UNLOADED
```

`LOAD`, `BUILD`, and `REBUILD` are all entered from `UNLOADED`.

The distinction is not whether a resident Project exists. The distinction is
which persisted/construction state the operation is allowed to use:

```text
LOAD
    restore the last committed final G

BUILD
    reuse the last successful construction baseline
    and incrementally construct the current Project

REBUILD
    ignore the old incremental baseline
    and construct a fresh lineage
```

There is no `SAVE` lifecycle stage. Durability is part of the successful
`BUILD`/`REBUILD` coordinated commit.

## Lifecycle

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

Preconditions:

```text
LOAD     requires UNLOADED
BUILD    requires UNLOADED
REBUILD  requires UNLOADED
UNLOAD   requires LOADED
```

A resident Project is never the input baseline for BUILD.

If source/configuration files have changed, the previously compiled final G no
longer represents the current Project. BUILD therefore never keeps an old
resident Project published while constructing a new one.

A failed BUILD discards only its candidate/overlay state. The last successful
persisted baseline remains available as an acceleration baseline for a later
BUILD, but no resident Project is published and the Server remains `UNLOADED`.

LOAD, BUILD, and REBUILD never substitute for each other.

## Mode-specific construction contexts

There is no universal Project construction context and no shared
`builder_context`.

```text
load_context
    Server settings

build_context
    Server settings
    committed baseline views
    candidate SourceSave/DB/final-G overlays
    root preprocessing configuration

rebuild_context
    Server settings
    manifest
    fresh File Context
    fresh lexical construction state
    fresh string_table
    fresh identity_space
    root preprocessing configuration
```

The context object is only the lifetime owner for one operation. SourceSave,
File Context, string storage, Semantic DB, Parser state, Builder state, and
final-G construction remain separate subsystems with explicit ownership.

ABI originates from `server.json`.

The root `preprocessor_configuration` originates from the root `project.json`
and is construction input only.

## Resident Project

Target resident state:

```text
project
    +-- final Graph / compiled G
    +-- Runtime
    `-- SHM
```

Resident Project contains only state required while the compiled Project is
active.

It must not own BUILD acceleration state:

```text
project.json composition
configuration manifest
SourceSave
File Context
lexical cache
string canonicalization index
Semantic identity construction index
Parser/frontend cache
SourceContribution / Builder provenance
other DB/build-cache state
```

Those belong to the persisted construction baseline and to temporary BUILD or
REBUILD operation state.

BUILD receives the Project entry path explicitly. Resident `project` therefore
does not exist merely to remember which baseline BUILD should open.

## LOAD

LOAD starts only from `UNLOADED`.

It restores the last committed final compiled state:

```text
persisted final G
    -> Runtime
    -> SHM
    -> resident Project
```

LOAD does not:

```text
parse project.json for construction
compose Project configuration
open BUILD-only DB state
run File Context change detection
run Parser
run Builder
validate the current source tree
```

BUILD-only SourceSave/DB artifacts are not required to enter runtime READY
state.

LOAD failure leaves `UNLOADED`.

## REBUILD

REBUILD starts only from `UNLOADED` and starts a fresh construction lineage.

```text
root project.json
    -> recursive Project configuration composition
    -> root preprocessor configuration
    -> fresh configuration manifest
    -> fresh File Context / SourceSave state
    -> fresh lexical construction
    -> fresh string_id space
    -> fresh identity_ref space
    -> Parser / Semantic
    -> fresh DB
    -> Assign resolution
    -> complete dependency topology
    -> fresh final G
    -> Runtime
    -> SHM
    -> coordinated persisted-baseline commit
    -> resident Project
```

REBUILD ignores the previous incremental construction state.

REBUILD is the reset/compaction boundary:

```text
file_id      may be reassigned
string_id    may be reassigned
identity_ref may be reassigned
Graph handles may be reassigned
stale BUILD-only history is reclaimed
```

### Current implementation boundary

The current V4 implementation completes:

```text
root project.json
    -> recursive type:"project" composition
    -> validation of every participating project.json
    -> root preprocessor_configuration
    -> project_configuration_manifest
    -> aggregate project_configuration_hash
    -> flat File Context population
         project/header/source/assign nodes
         fresh file_id + file_kind
    -> Header/Source exact-byte materialization
    -> complete-file lexical generation
    -> sparse preprocessing directive anchors
    -> deterministic executed quoted-include closure
         append-only discovered Header file_id values
         direct File Context dependency staging
    -> Assign exact-byte materialization
```

Cardinality is semantic, not generic identity policy:

```text
header  -> repeated use allowed
source  -> duplicate declaration is an error
assign  -> duplicate declaration is an error
project -> duplicate reference is an error
           active ancestor reference is a cycle error
```

File IDs are assigned root-first in declaration-order DFS. There is no sort.

The current implementation stops before:

```text
Parser/Semantic construction
identity_ref / identity_space
Assign grammar and semantic variable resolution
terminal REBUILD dependency-topology finalization
final SourceSave image construction
DB persistence
final-G construction
Runtime
SHM
coordinated baseline commit
```

The persistence subsystem already provides the final A/B artifact layout and
the `source.bin` encoder/validator. REBUILD must not invoke the SourceSave
encoder until Semantic/Assign processing has emitted every direct dependency
and `finalize_dependency_topology()` has completed.

Because no final G is produced yet, the incomplete REBUILD path must not publish
any candidate construction artifact as the new committed baseline.

## BUILD

BUILD starts only from `UNLOADED` and receives the root Project path explicitly.

BUILD uses the last successful persisted construction baseline as acceleration
state:

```text
last successful baseline
    +-- configuration proof
    +-- SourceSave
    +-- DB
    `-- final G / final-G build lineage state
             |
             + current Project files
             |
             v
          BUILD
             |
             +-- exact dirty detection
             +-- old reverse dependency closure
             +-- sparse SourceSave candidate
             +-- sparse DB candidate
             +-- affected frontend / Semantic work
             +-- sparse final-G construction
             +-- validation
             `-- coordinated commit
                     |
                     v
                  LOADED
```

The previous final G is not kept published during BUILD. It is baseline data
used only where sparse construction needs previous compiled slots/state.

### BUILD lineage identity

A successful REBUILD starts a new BUILD lineage.

Across successful BUILDs in that lineage:

```text
existing file_id      values are preserved
existing string_id    values are preserved
existing identity_ref values are preserved

new files       append new file_id values
new spellings   append new string_id values
new semantic WHO values append new identity_ref values
```

IDs are not renumbered or recycled by BUILD.

Removed entities/files may leave historical slots. REBUILD is the compaction
boundary.

### Configuration proof fast path

BUILD first opens the committed configuration manifest.

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
    -> no configuration parse
    -> no recomposition
    -> proceed directly to SourceSave/File Context change detection
```

If any configuration input differs or disappears:

```text
recompose from root
    -> discover added/removed/reordered Project inputs
    -> construct candidate configuration proof
```

Configuration recomposition does not itself destroy the old baseline.

### Physical dirty detection

SourceSave owns the persisted physical baseline:

```text
file_id
path / file_kind
current-lineage membership
content hash
optional native change token
direct dependency topology
```

Unchanged files are proved without reading bytes when possible. If proof is not
available, BUILD reads exact bytes and compares the content hash.

BUILD first derives the exact physical dirty set.

### Affected closure

The affected set is computed from the **committed old reverse topology** before
candidate dependency replacement:

```text
dirty file_id set
    -> walk persisted dependents
    -> affected closure
```

This is why direct reverse adjacency is first-class persisted construction data.

BUILD must not rebuild the complete `O(F + E)` topology merely to discover the
affected set.

### Frontend reuse

For a dirty physical file:

```text
new exact bytes
    -> re-lex
    -> new directive anchors
    -> affected preprocessing / Parser work
```

For an unchanged but semantically affected file:

```text
reuse persisted exact bytes / lexical facts
    -> rerun only required preprocessing / Parser / Semantic work
```

The compact lexical representation is therefore DB/build-cache data across
BUILD, not resident runtime Project state.

### Candidate and failure semantics

BUILD works against immutable committed baseline state plus disposable mutable
overlays/candidates.

A failed BUILD:

```text
discard candidate SourceSave/DB/final-G changes
keep the last successful persisted baseline intact
publish no resident Project
remain UNLOADED
```

The old baseline may accelerate the next BUILD after the user fixes the source
tree. The old final G is not treated as the current runnable Project.

A successful BUILD:

```text
validate all candidate artifacts
    -> coordinated durable commit
    -> make the new baseline authoritative
    -> publish Runtime/SHM resident Project
```

There is no explicit SAVE stage.

### Current implementation boundary

The current code already implements the configuration-manifest preflight:

```text
load project.manifest
verify every configuration input
recompose when one input changed
compare aggregate configuration hash
```

However, the current C++ entry path still uses the obsolete contract
`LOADED -> BUILD` and takes the resident Project as its BUILD input.

The next implementation correction must change that entry boundary to:

```text
UNLOADED
    -> BUILD <project-path>
    -> persisted baseline
```

before File Context/string/Semantic baseline restore is implemented.

## LOAD Implementation Boundary

LOAD restores a committed final G. It does not validate `project.json` as a
substitute for persisted runtime state and must never publish a placeholder
resident Project.

Until committed final-G/Runtime/SHM restore exists:

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

Resolved physical paths and `filesystem_path_key` values are temporary construction
state and are never persisted in the manifest.

Filesystem equivalence used for identity/cycle/duplicate detection belongs
to the common `filesystem_path.hpp` boundary:

```text
Windows -> invariant Unicode case-insensitive key
POSIX   -> case-sensitive key
```

Project path resolution and filesystem-key construction are separate fail-closed
boundaries.

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

## Persisted Build Baseline

The committed construction/runtime baseline belongs under the root Project
artifact directory:

```text
<root-project-dir>/
    .serverengine/
        <root-project.json filename>/
            committed baseline
```

Logically one successful baseline contains files whose Server-wide names come
from `server.json.settings.files`:

```text
configuration proof
    settings.files.manifest

SourceSave
    physical file identity/state
    current-lineage membership
    forward/reverse file topology

DB
    BUILD-only reusable construction state
    string/semantic identity lineage
    retained lexical/frontend facts
    semantic contributions / Builder provenance

final G
    the one compiled result required by LOAD/Runtime
```

The artifact roles use the Server-wide configured names
`settings.files.manifest/source_save/database/compiled/baseline`. Their binary
formats remain versioned implementation contracts.

The physical persistence layout is A/B:

```text
.serverengine/<root-project.json>/
    baseline.bin
    slot0/{project.manifest, source.bin, database.bin, compiled.bin}
    slot1/{project.manifest, source.bin, database.bin, compiled.bin}
```

Only `baseline.bin` selects the authoritative slot. Slot names are persistence
mechanics, not Project generations.

`source.bin` now has a concrete versioned/checksummed image boundary. It is
encoded only from terminally finalized File Context topology and contains:

```text
file_id order
UTF-8 physical path
file_kind
current-lineage membership flag
exact content hash
optional native change token
direct dependency range
direct dependent range
forward file_id arena
reverse file_id arena
```

The image validator reconstructs reverse adjacency from forward adjacency in
`O(F + E)` and rejects inconsistent topology. Path and forward-edge ranges are
also required to use one canonical contiguous encoding.

`database.bin` now has a sectioned versioned/checksummed base image:

```text
strings
    dense string_id order
    canonical spelling bytes

lexical records
    dense file_id order
    word/directive ranges
    token counts

lexical words
    one canonical flat word arena independent of CPU lanes

lexical directives
    one canonical flat anchor arena independent of CPU lanes
```

Semantic identity/facts and Builder provenance will be added as additional DB
sections. The persisted representation deliberately does not retain transient
per-lane lexical arenas.

`project.manifest` remains versioned, checksummed, and fail-closed, but its own
temporary-file replacement is not the final multi-artifact commit model.

Once the full baseline exists, all candidate artifacts must be prepared and
validated before one coordinated commit makes them authoritative.

There is no Graph history:

```text
no persisted G0/G1/G2 chain
no runtime generation history
```

Only the last successfully committed final G is current.

An implementation may retain older immutable transaction files temporarily for
crash-safe replacement, but those files are persistence mechanics, not Project
semantic generations.

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
    -> make_filesystem_path_key()
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
    physical proof state
    dependency topology
```

Syntax routing is explicit:

```text
file_kind::project -> Project configuration syntax
file_kind::header  -> Type syntax
file_kind::source  -> Source syntax
file_kind::assign  -> Assignment/connection syntax
```

The parsers remain separate semantic domains:

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

`file_context` contains neither Parser semantic state nor Graph state.

### `file_id` lifetime

`file_id` is dense and 1-based.

```text
REBUILD
    fresh file_id space

BUILD
    restore/bind committed file slots
    preserve existing file_id values
    append IDs for newly discovered physical files
```

A file removed from the current Project does not make its historical slot
available for reuse inside the same BUILD lineage.

Physical existence and current Project membership are distinct concepts:

```text
known file_id
current-lineage member?
physical file present?
```

BUILD may reactivate a previously known physical identity without inventing a
different `file_id`.

REBUILD may compact/reassign all slots.

### Storage

The logical storage remains compact SoA indexed by `file_id`:

```text
file_record[]              16 bytes / file
file_physical_record[]     64 bytes / file
file_dependency_record[]   16 bytes / file
native_path_chars[]        native-character arena
path_index[]                8 bytes / slot
forward_edges[]             4 bytes / direct edge
reverse_edges[]             4 bytes / direct edge
```

The current implementation owns all of these arrays directly for a fresh
construction.

BUILD will add baseline-view + mutable-overlay capability; it must not begin by
copying every committed file record merely to change a small subset.

The physical path is retained exactly for I/O. Platform-equivalence keys remain
transient lookup values.

`path_hash` is never durable identity.

### Physical acquisition

The existing split remains the physical change-detection contract:

```text
prepare_acquire()
    -> borrowed file_acquire_job

execute_acquire()
    -> native token proof when available
    -> otherwise stable read + SHA-256

apply_acquire()
    -> update candidate physical state
    -> report exact content change
```

Change tokens are proof optimizations only.

`construction_content_hash` remains a byte-content aggregate only. Path, role,
topology, semantic identity, and DB state belong to higher layers.

### Composition rules

Project composition owns syntax-domain cardinality:

```text
header
    reusable declaration input

source
    unique semantic construction unit

assign
    unique user connection-description input

project
    unique composed subtree
    active ancestor means cycle
```

`file_context` owns physical identity, not these language/configuration rules.

The same acceptance rules apply to REBUILD and BUILD recomposition.

## File Dependency Topology

`file_id` is the only identity of a construction-input dependency node.

There is no:

```text
graph_node_id
dependency_id
edge_id
stable_dependency_id
```

A direct relation is only:

```text
file_id -> file_id
```

For every file the logical topology exposes:

```text
dependencies(file_id)
dependents(file_id)
```

Only direct relations are stored.

### REBUILD topology

REBUILD stages every direct relation from all syntax domains and, after
dependency discovery reaches closure, performs one complete finalization.

The existing algorithm is appropriate for REBUILD:

```text
staged edges
    -> linear counting/grouping
    -> dense-marker duplicate collapse
    -> exact forward arena
    -> exact reverse arena
```

Complexity:

```text
O(F + E)
NO SORT
NO hash lookup for final grouping
```

### BUILD topology

BUILD must not run the complete REBUILD finalizer merely because one or a few
files changed.

BUILD begins with committed forward/reverse adjacency.

The order is:

```text
1. detect physical dirty files
2. compute affected closure using OLD committed dependents
3. rebuild only dependency contributions whose owners are affected/changed
4. publish the candidate current topology as part of the coordinated commit
```

The physical encoding used to support sparse owner replacement is not frozen
yet. It may use overlay/versioned/append-only techniques as long as the logical
contract remains one direct file topology and lookup remains compact.

There is still no separate BUILD graph identity.

### Syntax producers

Project composition stages explicit direct relations:

```text
project -> child project
project -> header
project -> source
project -> assign
```

Header, Source, and Assign syntax add only their own resolved direct relations.

Assign dependency emission occurs only after Semantic identity exists.

Dependency topology belongs to SourceSave/DB construction state and is never
resident runtime Project state.

## Publication Contract

BUILD and REBUILD construct candidates against a non-authoritative operation
state.

Nothing becomes authoritative merely because an intermediate file was written
or an in-memory subsystem completed.

Successful publication is one coordinated lifecycle boundary:

```text
candidate configuration proof
candidate SourceSave
candidate DB
candidate final G
candidate Runtime/SHM preparation
        |
        v
validate
        |
        v
coordinated durable commit
        |
        v
publish resident Project
```

Failure before that boundary:

```text
REBUILD failure
    -> discard fresh candidate
    -> preserve previous persisted baseline if one exists
    -> UNLOADED

BUILD failure
    -> discard sparse candidate overlays
    -> preserve last successful persisted baseline
    -> publish no resident Project
    -> UNLOADED
```

The previous final G may remain persisted as the last successful baseline, but
it is not treated as the current runnable Project after a failed BUILD against
changed source state.

## Architectural Invariants

1. One Server owns zero or one resident Project.
2. `server_context.project == nullptr` exactly means `UNLOADED`.
3. LOAD, BUILD, and REBUILD require `UNLOADED`.
4. UNLOAD requires `LOADED`.
5. LOAD, BUILD, and REBUILD receive the Project path explicitly.
6. BUILD consumes the last successful persisted baseline, never the resident Project.
7. Failure of LOAD, BUILD, or REBUILD leaves `UNLOADED`.
8. Resident Project contains runtime-required state only.
9. There is no universal `project_context`.
10. There is no SAVE lifecycle stage; BUILD/REBUILD own durability.
11. One successful REBUILD starts one BUILD lineage.
12. `file_id`, `string_id`, and `identity_ref` preserve existing numeric values across BUILD within that lineage.
13. REBUILD may reassign/compact `file_id`, `string_id`, `identity_ref`, and Graph handles.
14. Removed historical identity slots are not recycled by BUILD.
15. Configuration composition is root-first declaration-order DFS and is never sorted.
16. Change tokens are proof optimizations, never identity.
17. Per-file SHA-256 identifies exact bytes.
18. Candidate construction/persistence state becomes authoritative only at the coordinated commit boundary.
19. Project absolute-path resolution is fail-closed.
20. `file_id` is the only identity of a file-dependency node.
21. Forward and reverse file adjacency are first-class persisted BUILD data.
22. REBUILD may finalize topology with one `O(F + E)` pass.
23. BUILD computes the affected closure from committed reverse topology before sparse dependency replacement.
24. `string_id` is textual identity only.
25. `identity_ref` is semantic WHO only.
26. Semantic declaration/definition state is DB state, not identity state.
27. Graph handles identify locations in the final compiled result and are not semantic identity.
28. Only one final G is current; V4 does not persist a semantic Graph-generation history.

