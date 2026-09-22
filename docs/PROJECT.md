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
    restore G from the last committed compiled artifact

BUILD
    reuse persisted construction state
    to avoid repeating unchanged work
    -> G

REBUILD
    ignore previous BUILD acceleration state
    construct fresh build lineage state
    -> G
```

There is no `SAVE` lifecycle stage. Durability is part of the successful
`BUILD`/`REBUILD` artifact replacement.

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

A resident Project is never input state for BUILD.

If source/configuration files have changed, the previously compiled final G no
longer represents the current Project. BUILD therefore never keeps an old
resident Project published while constructing a new one.

A failed BUILD discards only its temporary operation state. Persisted BUILD
artifacts remain available for a later BUILD, but no resident Project is
published and the Server remains `UNLOADED`.

LOAD, BUILD, and REBUILD never substitute for each other.

## One-G construction model

Server Engine V4 has one Graph concept:

```text
G
```

There is no architectural:

```text
G0
Gn
Gn+1
candidate G
current G + next G
Graph publication generation
```

The three lifecycle operations differ only in how `G` is obtained:

```text
LOAD
    compiled.bin
        -> G

BUILD
    persisted BUILD state
    + current inputs
        -> reuse unchanged construction work where useful
        -> G

REBUILD
    current inputs
        -> fresh construction lineage
        -> G
```

Persisted artifacts do not introduce another Project or Graph lifetime.
`compiled.bin` is the persisted compiled Project consumed by LOAD.
`project.manifest`, `source.bin`, and `database.bin` are persisted BUILD
acceleration/lineage state.

## G storage boundary

`G` is the final compiled semantic Project representation.

```text
identity_ref
    semantic WHO

type_handle / object_handle / link_handle
    location inside one G

type_ref
    compact Graph-local type expression
```

`type_ref` is four bytes:

```text
[kind:2][payload:30]

intrinsic -> intrinsic_type
named     -> type_handle slot
derived   -> canonical derived-type slot
```

Intrinsic and named references require no lookup table. Derived expressions are
canonicalized by G.

G owns compact arrays for:

```text
types
type identities
members
objects
object identities
links
derived type expressions
```

The `identity_ref -> Graph location` map is dense by `identity_ref.slot()` and
stores one four-byte locator per semantic identity slot. This gives deterministic
O(1) type/object lookup without a hash table or sort.

Record definitions own contiguous member ranges. `member_index` is local to one
record type and is never a global identity.

There is no facts layer, Semantic DB, Builder, candidate Graph, prepared Graph,
or Graph-generation object between Parser/Semantic and G.

REBUILD operation state owns one temporary `G`; Parser/Semantic writes directly
into it.

### Construction semantics in G

Construction is semantic data, not source identity.

```text
identity_ref
    WHO

type_ref
    WHAT TYPE

construction_value
    HOW A RECORD MEMBER IS DEFAULT/INITIALIZED

object flags
    WHETHER A PROJECT OBJECT USES NON-DEFAULT INITIALIZATION

link_record
    HOW PROJECT OBJECT FIELDS ARE CONNECTED
```

`construction_value` is a pointer-free 16-byte normalized value. The current
construction slice supports zero/default, signed/unsigned decimal constants,
real constants, booleans/null, and local record-member reference bindings.

To keep hot member records compact, normalized member construction is stored in
a parallel cold array:

```text
member_record[12 B]    member_construction[16 B]
object_entry[8 B]      { type_ref, flags }
```

Project object initialization is not normalized into an object
`construction_value`. `object_entry.flags` records whether the declaration uses
non-default object initialization. Runtime/ABI construction remains responsible
for deciding whether that capability is supported.

Namespace-scope `static` and `inline` do not alter the canonical object identity
key `(parent, name, identity_kind::object)`.

Managed constructor syntax is normalized before `G.define_record()` and does not
survive as a constructor object or executable program in G.

```cpp
struct A {
    int& in;
    int out;

    A() : in(out), out(5) {
        in = out;
        out = 5;
    }
};
```

normalizes to:

```text
in  -> member_binding(out)
out -> unsigned_integer(5)
```

Construction precedence is:

```text
member declaration default
    -> constructor initializer list
    -> constructor body assignments
    -> final member_construction[]
```

For reference members, repeating the same constructor binding is accepted.
Rebinding the same reference member to a different member in a later constructor
operation is a semantic error. For value members, a later constructor operation
replaces the earlier/default construction value.

The supported constructor forms are no-argument managed constructors:

```cpp
A();
A() = default;
A() noexcept { ... }
A() : field(value), ref(other) { field = value; ref = other; }
```

Constructor parameters and arbitrary constructor statements/expressions remain
fail-closed. Nonempty aggregate member construction, arrays, and Runtime
materialization remain later capabilities.

The direct Parser slice also accepts Project objects and static links:

```cpp
Device A;
Device B{};

B.IN = A.OUT;
```

A repeated identical link is idempotent. A second different source for the same
target endpoint is a semantic conflict.

## Assign user-data boundary

Assign is not part of C++ semantic G.

```text
.assign bytes
    -> assign parser
    -> assign_table
```

Supported line forms are:

```text
source<TAB>target
target=source
```

Both produce the same ordered record:

```text
{ source, target }
```

`assign_table` stores 16-byte offset/length records over one compact char arena.
It intentionally has no `string_id`, `identity_ref`, Graph handle, hash lookup,
sort, semantic variable validation, Runtime binding, or dependency-edge
emission.

Assign data belongs to the compiled Project result because Studio/user consumers
need it. When `compiled.bin` persistence is implemented, the ordered Assign
table is persisted/restored alongside G; it is not BUILD acceleration state.

## Mode-specific construction contexts

There is no universal Project construction context and no shared
`builder_context`.

```text
load_context
    Server settings

build_context
    Server settings
    persisted BUILD-state views
    temporary BUILD reuse/change state
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
File Context, string storage, identity construction, lexical/frontend state,
Parser/Semantic execution, G construction, and persistence remain separate
responsibilities with explicit ownership.

ABI originates from `server.json`.

## ABI-derived Runtime layout

ABI layout is derived state, not part of `G`:

```text
G
+ server_settings_configuration.abi
    -> ABI layout
    -> Runtime / SHM
```

The exact reuse identity of a persisted ABI layout is:

```text
abi_layout_key
    target
    pack
```

`abi_layout_key` is compared exactly; it is not a hash. A compatible persisted
layout may be reused to accelerate LOAD or BUILD. A mismatch means the layout
must be recomputed from `G` under the current Server ABI; it does not make `G`
a different Graph or introduce another Graph state.

If ABI layout is persisted, it belongs with the compiled/runtime artifact as
derived acceleration. It is not BUILD semantic state and does not belong in
`database.bin`.

The current implementation provides only the ABI compatibility key boundary.
Actual ABI layout records are intentionally deferred until the type contract of
`G` exists.

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
Parser/frontend temporary state
BUILD lineage/reuse metadata
other DB/build-acceleration state
```

Those belong to persisted BUILD acceleration state and temporary BUILD or
REBUILD operation state.

BUILD receives the Project entry path explicitly. Resident `project` therefore
does not exist merely to remember which persisted BUILD state should be opened.

## LOAD

LOAD starts only from `UNLOADED`.

It restores the last committed final compiled state:

```text
persisted final G
    + compatible persisted ABI layout, when available
        -> reuse layout
    otherwise
        -> derive layout from G + Server ABI
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
run Parser/Semantic construction
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
    -> parse Assign inputs -> assign_table
    -> Parser / Semantic -> G
    -> complete dependency topology
    -> encode BUILD acceleration state
    -> derive ABI layout from G + Server ABI
    -> persist compiled G
       + optional compatible ABI-layout acceleration
    -> Runtime
    -> SHM
    -> replace persisted artifacts
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
    -> Assign exact-byte materialization
    -> ordered Assign user-table construction
    -> single Parser/Semantic preprocessing execution
         active quoted-include discovery
         append-only discovered Header file_id values
         direct File Context dependency staging
         -> G
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
remaining C++ declaration semantics beyond the first direct Parser slice
final SourceSave image construction
completed DB persistence
successful-operation compiled.bin replacement
ABI-layout derivation/persistence
Runtime
SHM
artifact replacement
```

The `identity_ref`/`identity_space` foundation already exists. Parser/Semantic has
not yet populated the Project semantic identities or G.

The persistence subsystem provides direct artifact paths and the `source.bin`
encoder/validator. Assign does not emit file dependencies. REBUILD finalizes the
File Context topology after Parser/Semantic include discovery; SourceSave
encoding may consume that finalized topology.

Because no G is produced yet, the incomplete REBUILD path must not publish any
partial construction artifact as newly persisted Project state.

## BUILD

BUILD starts only from `UNLOADED` and receives the root Project path explicitly.

BUILD uses persisted construction artifacts as acceleration state:

```text
persisted BUILD state
    +-- configuration proof
    +-- SourceSave
    +-- DB
    `-- compiled G
             |
             + current Project files
             |
             v
          BUILD
             |
             +-- exact dirty detection
             +-- old reverse dependency closure
             +-- reuse unchanged persisted construction state
             +-- affected frontend / Parser / Semantic work
             +-- construct G
             +-- validate complete artifact set
             `-- replace persisted artifacts
                     |
                     v
                  LOADED
```

The previously persisted compiled G is not a `Gn` object and BUILD does not
create a `Gn+1` object. Reusing unchanged compiled storage, if an implementation
chooses to do so, is only a storage/performance optimization while constructing
the one resulting `G`.

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
    -> construct the current configuration proof in temporary BUILD state
```

Configuration recomposition does not modify persisted BUILD state.

### Physical dirty detection

SourceSave owns persisted physical BUILD state:

```text
file_id
path / file_kind
current-lineage membership
content hash
optional native change token
direct dependency topology
```

`source.bin` deliberately does not copy Project file contents. The physical
Project files remain the source of exact bytes; retained lexical state belongs
to `database.bin` when BUILD reuse requires it.

BUILD memory-maps `source.bin` read-only directly and performs an O(1),
allocation-free `source_save_view::bind()`. Records and edges fail closed as
BUILD naturally visits them; BUILD does not perform a separate O(F + E)
topology-validation pass merely to open persisted SourceSave state.

On Windows/NTFS, `source.bin` persists one volume USN checkpoint plus compact
open-addressed identity indexes:

```text
file_reference -> file_id
directory file_reference -> topology-watch flags
```

Before BUILD dirty detection starts, V4 captures the journal checkpoint that may
be persisted by the resulting BUILD. The persisted `source.bin` checkpoint is still the start of dirty detection.

BUILD then reads the volume journal once from the committed `next_usn` to the
current journal position. Matching data-change records mark files for rebuild.
A dense bitset emits them in ascending `file_id` order without sorting and
without reopening every file on the normal journal path.

Any filesystem event after the newly captured checkpoint remains visible to the
next BUILD. It is acceptable for an event near that boundary to be rebuilt by
both BUILDs; the contract prevents loss rather than requiring a filesystem
snapshot.

Journal discontinuity, unsupported filesystems, mixed-volume construction, or a
rename/create/delete/hard-link/reparse event that invalidates the fast path
falls back to the portable full scan. That fallback does not advance the
persisted journal checkpoint.

The portable fallback deliberately hashes current file bytes; size/mtime equality
is not treated as exact content proof.

`scan_source_save_changes()` therefore produces the exact dirty `file_id` set
without O(F) per-file USN opens on the normal Windows path.
`collect_source_save_affected()` then walks the OLD committed reverse topology
before any affected dependency relation is replaced.

### Affected closure

The affected set is computed from the **persisted old reverse topology** before
affected dependency relations are recomputed:

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
reuse persisted lexical state where sufficient
materialize current physical bytes only when the frontend needs source spelling
    -> rerun only required preprocessing / Parser / Semantic work
```

`source.bin` never stores a second copy of Project source bytes.

The compact lexical representation is therefore DB/build-cache data across
BUILD, not resident runtime Project state.

### mmap-native compiled Project

The compiled artifact follows the proven V3 persistence principle but not the
V3 generation/baseline architecture:

```text
compiled.bin
    -> mmap
    -> compiled_project_view
```

LOAD does not reconstruct `string_table`, `identity_space`, or mutable `graph`.
The mapped bytes are the read-only compiled Project representation.

V4 `compiled.bin` v1 uses a 256-byte header, a fixed 16-entry section directory,
64-byte aligned sections, canonical little-endian integers, per-section CRC64,
directory CRC64, and header CRC64.

Sections are:

```text
string_core
string_index
string_bytes
identity_core
identity_index
types
type_identities
members
member_construction
derived_types
objects
object_identities
links
graph_identity_index
assign_records
assign_bytes
```

Numeric `string_id`, `identity_ref`, `type_handle`, `object_handle`,
`link_handle`, `member_index`, and `type_ref` slots are preserved exactly.
There is no ID remap and no mutable-container reconstruction on LOAD.

`string_index`, `identity_index`, and `graph_identity_index` are persisted read
accelerators. They are not another semantic representation and do not create a
Semantic DB.

Assign remains user data: its records/bytes are persisted beside G but do not use
semantic IDs and do not participate in Graph lookup or Runtime binding.

The V3 concepts deliberately not carried into V4 are generations, baseline/A-B
slots, Project Manager, SAVE, and V3 build-cache/source-manager architecture.
V4 still has one G and the direct artifact layout.

### Persistence semantics

BUILD owns temporary operation state while constructing the new result.

The persisted files have separate roles:

```text
compiled.bin
    compiled Project state required by LOAD

project.manifest
source.bin
database.bin
    BUILD acceleration / lineage state
```

LOAD opens only `compiled.bin`.

BUILD opens the persisted construction artifacts it requires. If required BUILD
state is missing or invalid, incremental BUILD cannot proceed and REBUILD is
required.

A successful BUILD/REBUILD replaces the persisted artifact files produced by
that operation. There is no selector file or active/inactive persistence slot,
and there is no explicit SAVE lifecycle stage.

A failed BUILD publishes no resident Project and leaves the Server `UNLOADED`.

### Current implementation boundary

The current code already implements the configuration-manifest preflight:

```text
load project.manifest
verify every configuration input
recompose when one input changed
compare aggregate configuration hash
```

The current C++ BUILD entry already matches the lifecycle contract:

```text
UNLOADED
    -> BUILD <project-path>
    -> persisted BUILD artifacts
```

File Context/string/identity restore and the remaining BUILD reuse path are not
implemented yet.

## LOAD Implementation Boundary

LOAD opens only `compiled.bin`; it never validates `project.json` as a substitute
for persisted compiled state.

The normal LOAD hot path is:

```text
compiled.bin
    -> read-only mmap
    -> compiled_project_view::bind()
```

`bind()` validates the fixed format, header/directory CRCs, aligned section
extents, numeric slot bounds, and persisted index capacities. It does not scan
section payloads.

`verify_contents()` is the separate cold integrity/semantic audit. It verifies
per-section CRCs and deep persisted invariants and is not part of normal LOAD.

There is no mutable Graph/string/identity reconstruction and therefore no
allocation proportional to Project semantic size on LOAD.

Runtime/SHM construction and resident Project ownership of the mapping are still
not implemented, so successful structural bind currently ends with:

```text
project.load_incomplete
server_status::unsupported
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

G
    the compiled semantic result; it is not another Project identity namespace
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

## Persisted Project Artifacts

Project persistence lives under:

```text
<root-project-dir>/
    .serverengine/
        <root-project.json filename>/
            project.manifest
            source.bin
            database.bin
            compiled.bin
```

The artifacts have different consumers:

```text
compiled.bin
    mmap-native semantic string/identity state
    compiled G arrays + read indexes
    ordered Assign user table
    optional ABI-layout acceleration
    required by LOAD

project.manifest
    project.json configuration proof
    BUILD only

source.bin
    file_id lineage
    physical file identity/state
    forward/reverse dependency topology
    BUILD only

database.bin
    retained lexical state
    string_id / identity_ref lineage
    BUILD acceleration
    BUILD only
```

There is no selector file or active/inactive persistence slot.

LOAD requires only a valid `compiled.bin`.

BUILD requires the persisted BUILD state needed for incremental reuse. Missing
or invalid required BUILD state means REBUILD is required.

Each artifact keeps its own versioned/checksummed format. Replacing an artifact
is filesystem persistence mechanics, not another Project lifetime.

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
    references existing variables/endpoints
    writes resolved connections into G
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

BUILD will add persisted-view + mutable-overlay capability; it must not begin by
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
3. recompute only dependency relations whose owners are affected/changed
4. persist the resulting current topology as part of the artifact replacement
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

Dependency topology belongs to SourceSave construction state and is never
resident runtime Project state.

## Publication Contract

BUILD and REBUILD own only temporary, non-authoritative operation state until
the complete artifact set is ready.

Nothing becomes authoritative merely because an intermediate file was written
or an in-memory subsystem completed.

Successful publication is one coordinated lifecycle boundary:

```text
temporary configuration proof
temporary SourceSave state
temporary DB state
constructed G
prepared Runtime/SHM state
        |
        v
validate complete artifact set
        |
        v
write and validate inactive persistence slot
        |
        v
persisted artifact replacement
        |
        v
publish resident Project
```

Failure before that boundary:

```text
REBUILD failure
    -> discard temporary REBUILD state
    -> preserve previously persisted BUILD state if it exists
    -> UNLOADED

BUILD failure
    -> discard temporary BUILD state
    -> preserve persisted BUILD state
    -> publish no resident Project
    -> UNLOADED
```

The previously persisted compiled artifact may remain as the last successful
persisted BUILD state, but it is not treated as the current runnable Project after a failed
BUILD against changed source state.

## Architectural Invariants

1. One Server owns zero or one resident Project.
2. `server_context.project == nullptr` exactly means `UNLOADED`.
3. LOAD, BUILD, and REBUILD require `UNLOADED`.
4. UNLOAD requires `LOADED`.
5. LOAD, BUILD, and REBUILD receive the Project path explicitly.
6. BUILD consumes persisted BUILD state, never the resident Project.
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
18. Temporary construction/persistence state becomes authoritative only at the artifact replacement boundary.
19. Project absolute-path resolution is fail-closed.
20. `file_id` is the only identity of a file-dependency node.
21. Forward and reverse file adjacency are first-class persisted BUILD data.
22. REBUILD may finalize topology with one `O(F + E)` pass.
23. BUILD computes the affected closure from committed reverse topology before recomputing affected dependency relations.
24. `string_id` is textual identity only.
25. `identity_ref` is semantic WHO only.
26. `identity_ref` carries no declaration/definition state; Parser/Semantic writes the compiled semantic result directly into G.
27. Graph handles identify locations in the final compiled result and are not semantic identity.
28. V4 has one Graph concept, `G`; persistence reuse does not create Graph generations.
