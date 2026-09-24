# Project Architecture

## Purpose

Project lifecycle is mode-oriented:

```text
LOAD <project-path>
PUBLISH <project-path>
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

`LOAD`, `PUBLISH`, `BUILD`, and `REBUILD` are all entered from `UNLOADED`.

The operations differ only in how final G is obtained and whether BUILD
acceleration is produced:

```text
LOAD
    compiled.bin -> G
    no Parser/source construction

PUBLISH
    project.json + source inputs
    -> full source compiler
    -> G
    -> compiled.bin
    no BUILD acceleration output

BUILD
    reuse persisted construction state
    to avoid repeating unchanged work
    -> G

REBUILD
    same full source compiler as PUBLISH
    -> G
    -> compiled.bin
    + fresh BUILD acceleration state
```

After G exists, all modes converge on one architectural tail:

```text
G -> Runtime -> SHM -> Project
```

Runtime/SHM remains Phase 2; the current Phase-1 implementation publishes the
resident mmap-native compiled representation at that convergence boundary.

There is no `SAVE` lifecycle stage. `compiled.bin` is the critical semantic
persistence output of REBUILD and is the mmap-native compiled Project consumed
by LOAD/resident Project publication. `project.manifest`, `source.bin`, and
`database.bin` are BUILD-acceleration outputs. After construction reaches its
final immutable state, the four direct-mmap persistence branches are scheduled
in parallel. Failure of a BUILD-acceleration branch does not invalidate the new
G; it is reported as a warning and becomes a problem for the next BUILD. BUILD
has a different failure contract: a failed BUILD preserves the previously
persisted BUILD state.

## Lifecycle

```text
UNLOADED
    +-- LOAD <path> success ------> LOADED
    +-- LOAD <path> failure ------> UNLOADED
    +-- PUBLISH <path> success ---> LOADED
    +-- PUBLISH <path> failure ---> UNLOADED
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
PUBLISH  requires UNLOADED
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

LOAD, PUBLISH, BUILD, and REBUILD never substitute for each other.

`PUBLISH` is the reference full compiler path. `REBUILD` reuses that exact
compiler path and differs only by additionally persisting BUILD acceleration.
For identical source/configuration input, PUBLISH and REBUILD must produce the
same final semantic G.

## Semantic Source Provenance

G remains source-agnostic. Physical-file ownership is stored in a separate
Source Map that is part of the final compiled Project.

The canonical ownership unit is one semantic root execution:

```text
root file_id
    -> contiguous uint32 contribution-index range
```

Each contribution records both the physical file that supplied the semantic
token and a compact semantic datum:

```text
{ physical file_id, type declaration/definition | object | link }
```

This distinction is required because the same physical header may execute from
multiple semantic roots and produce different semantic identities under
different semantic/preprocessor contexts.

The Runtime/Studio-facing physical-file projection is derived from the canonical
root contributions:

```text
physical file_id
    -> contribution indices
```

It is a secondary index over the same contribution records, not another
canonicalization domain. If two semantic roots contribute the same datum from
the same physical file, two root-owned contribution records exist and the
physical-file view references both. Within one root/file pair repeated identical
contributions collapse, and a type definition dominates a declaration of the
same type.

Members are not duplicated in Source Map. A contributed type definition resolves
to its members through G.

File dependency topology and semantic source provenance are separate contracts.
Parser/Semantic also records root-local semantic dependencies on lineage-stable
type/object handles. Those dependency observations never enter the physical file
DAG and are not part of the LOAD/runtime compiled semantic image. REBUILD derives
their reverse root adjacency and persists it only in `source.bin` as BUILD
acceleration state.


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

The four G-acquisition operations differ only in how `G` is obtained:

```text
LOAD
    compiled.bin
        -> G

PUBLISH
    current inputs
        -> full source compiler
        -> G

BUILD
    persisted BUILD state
    + current inputs
        -> reuse unchanged construction work where useful
        -> G

REBUILD
    current inputs
        -> the same full source compiler as PUBLISH
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
member construction
objects
object identities
object construction
links
derived type expressions
```

`object_entry` remains the hot eight-byte object record. Its 32-bit state packs
one optional 30-bit one-based construction slot plus the `non_default` and
`internal_static` flags. Only objects with non-default initialization allocate a
16-byte entry in the sparse cold `object_construction` arena. Header
internal-linkage static objects and Source Project objects use the same object
handle space; storage semantics distinguish their roles.

The `identity_ref -> Graph location` map is dense by `identity_ref.slot()` and
stores one four-byte locator per semantic identity slot. This gives deterministic
O(1) type/object lookup without a hash table or sort.

Record definitions own contiguous member ranges. `member_index` is local to one
record type and is never a global identity.

There is no facts layer, Semantic DB, Builder, candidate Graph, prepared Graph,
or Graph-generation object between Parser/Semantic and G.

PUBLISH/REBUILD shared full-construction state owns one temporary `G`;
Parser/Semantic writes directly into it.


## Header / Source semantic boundary

Phase 1 has two semantic lines over one final G:

```text
Header
    C++ preprocessing / active quoted include
    -> Type domain
    -> record members / managed constructors
    -> namespace-scope internal-linkage static objects needed by type construction

semantic barrier

Source
    no C++ preprocessing / no include
    -> consumes completed Type domain
    -> Project objects
    -> object initialization
    -> links
```

The barrier is independent of `project.json` declaration order. A Source file
may appear before a Header in Project composition and still resolves against the
completed Header Type domain.

A Header namespace-scope `static` object is not a Source Project object. It uses
the existing Graph object handle/storage but has semantic-root-local internal
linkage. Example:

```cpp
static int a = 5;

struct A {
    int& b;
    A() : b(a) {}
};
```

`a` is retained as an internal-static Graph object with construction value `5`;
`A::b` retains an `object_binding` to that object handle. The packed
`identity_ref` format/key is unchanged: no linkage bit or new identity kind is
added. Construction creates a root-local semantic execution scope under the
current namespace only to distinguish internal-linkage WHO values belonging to
different Header roots.

## Source Provenance Map

`G` deliberately contains no `file_id`. Runtime/Studio still needs to answer
which semantic data came from which physical Project file, and sparse BUILD must
replace semantic work by the semantic root that produced it.

The final Project therefore has a separate cold Source Map:

```text
semantic root file_id
    -> contiguous contribution range

contribution
    -> physical file_id
    -> semantic datum

physical file_id
    -> compact secondary contribution-index range
```

A contribution is only one of:

```text
type declaration
type definition
object
link
```

Members are not duplicated in Source Map. A type definition identifies the
canonical type; its members are read from G.

Canonical ownership is the root-contiguous contribution array. There is no
cross-root contribution canonicalization and no root-index indirection. The
physical-file index contains uint32 positions into that same array.

Within one semantic root and physical file, repeated identical contributions are
collapsed without sorting. A type definition subsumes a declaration of the same
type in that same root/file pair.

This separation is intentional:

```text
G
    WHAT exists

Source Map
    WHERE it came from and WHICH semantic root produced it

File dependency topology
    WHICH files depend on which files
```

Source Map construction and persistence are implemented. `compiled.bin` v3 stores
root-contiguous `{physical file_id, source_data_ref}` records, root ranges,
file ranges + uint32 secondary indices, and UTF-8 file paths/kinds. LOAD queries
these sections directly; BUILD-only artifacts are not required for provenance
queries.

BUILD-only aggregate semantic-presence and semantic-dependency state is separate
from the physical file DAG and does not belong in G. Its persisted home is
`source.bin` beside physical dependency topology. `source.bin` v5 stores
`{declarations, definitions}` counters per Graph type, uint32 counters per
object/link, root-contiguous semantic dependency observations, and one sparse
mmap-native reverse hash index:

```text
semantic root file_id
    -> referenced type_handle / object_handle

type_handle / object_handle
    -> dependent semantic roots
```

The reverse hash index is sized by unique semantic dependency targets `U`,
while its dependent-root adjacency stores the actual semantic edges `E`.
Semantic dependency memory is therefore `O(U + E)`, not `O(|G|)`. Each root-owned contribution still increments presence; a definition
also increments declarations. Repeated inclusion within one root/file pair
counts once. Removing one owner subtracts only that ownership, preserving data
owned by other roots.

`source_map::finalize()` resolves semantic slots through the authoritative
`identity_space` and G dense lookup; it does not construct another semantic
slot-to-Graph hash index.

The cold `verify_source_save_presence()` audit recomputes counters from the
compiled Source Map and checks paths/kinds and Graph cardinalities across the two
artifacts. Older compiled v1/v2 or source v2 images require REBUILD. The current
lifecycle still returns unsupported and removes incomplete REBUILD artifacts;
this format change does not claim to complete sparse BUILD or resident Runtime
publication.

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

ABI layout is Phase-2 derived state, not part of `G` and not part of the current
Phase-1 persisted compiled format:

```text
G
+ server_settings_configuration.abi
    -> ABI layout
    -> Runtime / SHM
```

Its exact representation and any future persistence/reuse policy are intentionally
deferred until Phase 1 is complete.

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

Its Phase-1 responsibility is:

```text
compiled.bin
    -> read-only mmap
    -> compiled_project_view
    -> G
```

Runtime/SHM construction from G belongs to Phase 2.

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

Development is intentionally split into two boundaries:

```text
PHASE 1
LOAD / PUBLISH / BUILD / REBUILD
    -> compiled G
    -> resident Project semantic state

PHASE 2
resident compiled G
    -> Runtime
    -> SHM
```

The Phase-1 resident Project owns only runtime-required immutable compiled
semantic state: the `compiled.bin` mapping and `compiled_project_view`.
Construction state never crosses this boundary. ABI-derived Runtime layout and
SHM belong to Phase 2.

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

The current implementation reaches:

```text
current inputs
    -> File Context / lexical / preprocessing
    -> Parser/Semantic
    -> G
    -> finalized dependency topology
    -> parallel direct-mmap persistence
         compiled.bin
             -> exact layout
             -> encode + cold validation + flush
             -> reopen read-only
             -> resident Project owns compiled mapping/view

         source.bin
         database.bin
         project.manifest
             -> independent BUILD-acceleration persistence
             -> prepare/encode/validate/flush where applicable
```

The persistence fan-out uses the existing fixed construction execution lanes;
the owner thread handles the compiled branch while other lanes process
independent acceleration outputs. There is no work queue, mutex, per-artifact
task allocation, or full-size serialized `std::vector<std::byte>` image.

`compiled.bin` is the only persistence branch whose failure fails REBUILD.
Failures in `project.manifest`, `source.bin`, or `database.bin` are warnings.
Because REBUILD clears the previous artifact set before starting a fresh lineage,
a failed acceleration branch leaves missing/invalid state rather than a stale
previous baseline. The next BUILD must reject missing/invalid required
acceleration state and require REBUILD.

A failure of G construction, `compiled.bin` persistence, or resident publication
is still a REBUILD failure. The operation scope destroys mappings first, then
the existing REBUILD cleanup removes the artifact set.

The remaining Phase-1 lifecycle work is BUILD persisted-state reconstruction and
sparse affected rebuild to G. Runtime and SHM remain Phase 2.

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
             `-- construct G
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

`source.bin` deliberately does not copy Project file contents. Physical files
remain the change-acquisition inputs. `database.bin` v4 retains the exact
Header/Source snapshot bytes paired with their lexical state, so unchanged
semantic replay uses one immutable committed filesystem epoch without reopening
those files.

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

The portable fallback only enumerates every current persisted `file_id` as an
acquisition candidate; it performs no filesystem reads and creates no File
Context overlays. The shared exact classifier then reads/hashes each candidate
once. Only SHA-256-different or missing files become `semantic_changed`;
same-byte snapshots are discarded immediately.

`scan_source_save_change_candidates()` therefore separates physical change hints
from exact semantic change. `collect_source_save_affected()` walks the OLD
committed reverse topology from `semantic_changed` before any affected dependency
relation is replaced.

### Affected closure

The affected set is computed from the **persisted old reverse topology** before
affected dependency relations are recomputed:

```text
semantic_changed file_id set
    -> walk persisted dependents
    -> affected physical closure

affected Header/Source
    -> has OLD Project parent?
    -> affected semantic root
```

The physical DAG already contains both Project-declaration edges and active
include edges. Therefore reverse closure reaches every semantic root that used a
changed physical input, including roots that previously produced zero semantic
contributions. Root selection does not require a duplicated contribution->root
index in compiled.bin.

This is why direct reverse adjacency is first-class persisted construction data.

The reverse walk uses a dynamically grown open-addressed `file_id` membership
set sized by the visited closure, not a dense `file_count` marker. Therefore a
small incremental BUILD has:

```text
time   O(A + E_old(A))
memory O(A)
```

where `A` is the affected physical closure. BUILD does not zero or reconstruct
`O(F)` state merely to discover the affected set.

Physical reachability is not enough for the global Header/Source semantic model.
After `compiled.bin` is bound, BUILD expands the OLD invalidated semantic roots
through the sparse reverse semantic dependency index in `source.bin` and OLD
compiled Source Map ownership:

```text
physically/configuration-invalidated semantic roots
    -> OLD contributed type/object handles
    -> source.bin reverse semantic adjacency
    -> dependent semantic roots
    -> transitive closure
```

A Header type can therefore invalidate another Header or Source that consumed it
even when no physical `#include` edge exists. Source link endpoints record both
the referenced object and the resolved record type, so a record member-layout
change cannot silently preserve an obsolete persisted `member_index`.

The semantic closure uses a dynamically grown sparse root membership set and the
persisted O(1)-expected reverse hash index. Its work is proportional to visited
semantic roots and dependency edges rather than total Graph slot count.

### Frontend reuse

For an exact `semantic_changed` Header/Source:

```text
single-owner begin_replacement(file_id)
    -> stale database.bin lexical record hidden immediately

present changed file
    -> already-acquired File Context bytes
    -> parallel re-lex
    -> sparse native lexical replacement

missing changed file
    -> baseline remains masked
    -> no tokenization
    -> affected preprocessing decides whether the old include is still required
```

Replacement masking is completed before workers start. Parallel workers only
publish distinct replacement records into lane-local lexical arenas; the
replacement index is read-only during the parallel phase.

For an unchanged but semantically affected Header/Source:

```text
source spelling -> database.bin mmap
lexical state   -> database.bin mmap
    -> rerun only required preprocessing / Parser / Semantic work
```

No filesystem reopen, source-byte copy, or re-lex is required for the unchanged
file. `source.bin` never stores Project source bytes; `database.bin` owns the
exact committed Header/Source snapshot paired with its lexical stream.

The frontend representation is BUILD-cache state only and never resident Runtime
Project state.

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

V4 `compiled.bin` v5 uses a 256-byte header, a fixed 24-entry section directory,
64-byte aligned sections, canonical little-endian integers, per-section CRC64,
directory CRC64, and header CRC64. The v5 format adds persisted O(1)-expected
derived-type and link-target read indexes required by sparse BUILD; older v4
artifacts fail closed and require REBUILD.

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
object_construction
links
graph_identity_index
assign_records
assign_bytes
source_contributions
source_roots
source_files
source_file_indices
source_paths
derived_index
link_target_index
```

Numeric `string_id`, `identity_ref`, `type_handle`, `object_handle`,
`link_handle`, `member_index`, and `type_ref` slots are preserved exactly.
Object construction is persisted as a compact cold arena addressed by the
construction slot stored in each object record; default-constructed objects do
not allocate an arena entry.
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

BUILD and REBUILD intentionally have different persisted-failure contracts.

BUILD continues an existing lineage. A failed BUILD publishes no resident
Project, leaves the Server `UNLOADED`, and preserves the previously persisted
BUILD state for a later BUILD attempt.

REBUILD starts a fresh lineage. Before construction it removes:

```text
project.manifest
source.bin
database.bin
compiled.bin
```

This pre-clean prevents a failed best-effort acceleration write from exposing a
stale previous baseline as if it belonged to the new G.

REBUILD writes final artifact names directly; there is no `.tmp`, selector,
active/inactive slot, A/B generation, rollback artifact, or SAVE stage.
`compiled.bin` is mandatory. The three BUILD-acceleration outputs are
best-effort: their persistence failure is a warning and does not fail an
otherwise valid REBUILD. If the REBUILD operation itself fails, the existing
cleanup removes the artifact set again.

### Current implementation boundary

The current BUILD implementation reaches:

```text
load project.manifest
    -> verify configuration inputs
    -> read-only mmap/bind source.bin
    -> when configuration bytes changed:
         -> recompose CURRENT Project into sparse File Context overlays
         -> collect CURRENT Project-declared Header/Source semantic roots
         -> recover OLD Project-declared semantic roots from
            project.manifest + source.bin in O(P + E_project)
         -> classify root delta:
              removed root     -> invalidate only
              new root         -> replay only
              persistent physically affected root -> invalidate + replay
         -> if root preprocessor_hash changed:
              all OLD roots     -> invalidate
              all CURRENT roots -> replay
         -> appended CURRENT Header/Source roots enter lexical replacement
            even though they are absent from OLD SourceSave classification
    -> mmap-native normalized-path -> file_id lookup
    -> physical change-candidate discovery
    -> parallel exact acquire/hash classification
    -> semantic_changed
    -> OLD reverse dependency affected closure
    -> select physically affected semantic roots from OLD Project-parent edges
    -> merge physical affected roots with OLD/CURRENT composition delta
    -> when replay is required and Project composition itself was unchanged:
         -> read only root project.json to recover current preprocessor_configuration
         -> do not recompose the whole Project tree
    -> read-only mmap/bind compiled.bin
    -> bind append-only string_id / identity_ref overlays
    -> mmap/bind database.bin only when affected files require frontend reuse
    -> mask semantic_changed Header/Source lexical baselines
    -> parallel retokenize only present semantic_changed Header/Source files
```

The current C++ BUILD entry matches the lifecycle contract:

```text
UNLOADED
    -> BUILD <project-path>
    -> persisted BUILD artifacts
```

Sparse exact File Context mutation, per-file lexical replacement/reuse,
OLD/CURRENT Project-composition semantic-root delta, and physical affected-root
selection are implemented. Project composition does not finalize File Context
topology early: current Project edges remain staged so later Header include
replacement can extend the same sparse topology before the single finalization.
Selected-root Parser/Semantic reconstruction and final G construction are not
implemented yet.
The physical mechanism used by a successful BUILD to persist its new state is
intentionally not frozen yet; it must satisfy the separate BUILD failure
contract that preserves the previously persisted BUILD state.

## LOAD Implementation Boundary

LOAD opens only `compiled.bin`; it never validates `project.json` as a substitute
for persisted compiled state.

The normal LOAD path is:

```text
compiled.bin
    -> read-only mmap
    -> compiled_project_view::bind()
    -> resident Project owns mapping/view
    -> success
```

`bind()` validates the fixed format, header/directory CRCs, aligned section
extents, numeric slot bounds, and persisted index capacities. It does not scan
section payloads. `verify_contents()` remains the separate cold audit.

There is no mutable Graph/string/identity reconstruction and therefore no
allocation proportional to Project semantic size on LOAD. Opening or parsing
`project.json` is not part of the LOAD proof.

## Configuration Manifest

The complete composed Project configuration proof is represented by:

```text
project_configuration_manifest
    configuration_hash
    preprocessor_hash
    files[]
        normalized declared locator
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

project_preprocessor_hash
    SHA-256 of the ordered root predefine configuration
    used to distinguish composition-only edits from semantic-context edits

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
    exact Header/Source snapshot bytes
    retained per-file lexical state
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
construction. `source.bin` v4 additionally persists an 8-byte/slot open-addressed
path index `{stable fingerprint, file_id}`. REBUILD encodes this index directly
into the final `source.bin` mapping; no full transient copy of the index exists.
BUILD probes it directly from mmap, and full platform filesystem-key equality is
confirmed on a fingerprint match, so the fingerprint is an accelerator and
never file identity.

BUILD File Context now binds `source.bin` as an immutable baseline. Existing
`file_id` path/kind/topology reads stay mmap-backed, while a touched existing
file lazily acquires only a sparse native-path/physical/content overlay. Newly
discovered files append after the committed `file_id` range. No dense committed
file/path/physical array is reconstructed.

BUILD replaces dependency adjacency sparsely. An affected existing source is
explicitly marked as a complete forward-adjacency replacement; newly discovered
sources are replacement-owned automatically. Finalization reads only the old
outgoing edges of replaced sources, deduplicates their new edges, and computes
reverse add/remove deltas. Unaffected forward and reverse adjacency remains
mmap-backed.

The work is proportional to replaced sources plus their old and new outgoing
edges, not to the full File Context: `O(A + E_old(A) + E_new(A))`. Reverse
dependents are exposed as baseline-plus-delta views, so changing one edge into a
popular Header does not copy that Header's complete dependent list.

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

### BUILD source / lexical baseline

`database.bin` is the retained frontend BUILD cache. One persisted file record
pairs exact Header/Source snapshot bytes with the lexical stream produced from
that same snapshot. BUILD binds source bytes through a borrowed
`file_content_baseline_view` and lexical state through `lexical_baseline_view`;
File Context and `lexical_generation` remain independent from database persistence
types.

```text
unchanged committed file
    -> source spelling bytes from database.bin mmap
    -> words/directives/token_count from database.bin mmap

affected committed file
    -> begin_replacement(file_id)
    -> baseline hidden immediately
    -> lexer publishes sparse native replacement

new file_id
    -> append-only local lexical record
```

Binding is O(1) in committed file count and allocates only bounded producer
arenas. BUILD does not allocate the dense `lexical_record[file_count]` tables
used by REBUILD; mutable memory grows with replaced/new lexical state.

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
4. persist the resulting current topology as part of the successful BUILD state
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

Header and Source preprocessing append their own resolved direct include
relations. Assign is raw user data and emits no dependency edge beyond the
`project -> assign` relation created by composition.

Dependency topology belongs to SourceSave construction state and is never
resident runtime Project state.

## Persistence Failure Contract

REBUILD and BUILD deliberately have different persistence semantics.

REBUILD is the fresh-lineage boundary:

```text
REBUILD start
    -> delete previous artifact set
    -> construct fresh lineage + G
    -> finalize construction state
    -> parallel persistence
         compiled.bin      REQUIRED
         source.bin        BUILD acceleration
         database.bin      BUILD acceleration
         project.manifest  BUILD acceleration
```

There is no inactive slot, `.tmp` artifact, A/B generation, selector, rollback
artifact, or coordinated persistence generation. `compiled.bin` is the
mmap-native semantic result and is required for REBUILD success and resident
publication. A failure to persist one of the other three files is emitted as a
warning; the new G remains valid, and the next BUILD is responsible for rejecting
missing/invalid acceleration state.

If G construction, `compiled.bin`, or resident publication fails, REBUILD closes
its operation-local mappings and removes the artifact set again. If the
filesystem refuses required cleanup deletion, REBUILD reports
`project.rebuild_cleanup_failed`, returns `io_error`, and remains `UNLOADED`.

BUILD continues an existing lineage. A failed BUILD discards only its temporary
operation state, preserves the previously persisted BUILD state, publishes no
resident Project, and leaves the Server `UNLOADED`.

Runtime/SHM publication is Phase 2 and is separate from this Phase-1 persistence
contract.

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
18. REBUILD writes final artifact paths directly; compiled.bin is mandatory, while manifest/source/database persistence is best-effort BUILD acceleration.
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
29. G contains no file ownership; physical-file semantic provenance belongs to the separate Source Map.
30. Source Map ownership is by semantic root; each contribution also records its physical file_id.
31. File dependency topology and semantic provenance are separate structures and must not be conflated.
