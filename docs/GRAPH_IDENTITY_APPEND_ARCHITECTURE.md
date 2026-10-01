# Graph Identity and Append-Only BUILD Architecture

Status: **Target architecture decision**  
Repository baseline: `proninb/ServerEngineV4`  
Decision baseline: `c5e73eca19065aec8018b875a6ef9ef76e1c73e3`  
Date: 2026-10-01

## Purpose

This document fixes the next Server Engine V4 Graph/BUILD direction.

The architectural distinction is:

```text
identity_ref = semantic WHO
Graph handle / physical slot = current WHERE
```

V4 must not preserve semantic correctness by treating physical Graph slots as entity identity. Normal BUILD may relocate an entity's physical payload without changing its `identity_ref`.

The target architecture is designed to make:

```text
sparse BUILD
    proportional to changed/affected semantic work

durable BUILD publication
    proportional to changed/affected persisted payload

Runtime construction
    independent from stale physical BUILD storage

Runtime execution
    direct native FIXED_DIRECT references
```

This supersedes the previously considered direction of making persisted type/object/link slot tombstones and historical Graph-handle lineage the primary V4 semantic mechanism.

## Core contract

### WHO

`identity_ref` is Project-local semantic identity:

```text
[kind:2][slot:30]
```

It answers:

```text
Which semantic entity is this?
```

Normal BUILD preserves an existing `identity_ref`. New semantic identities append after the persisted identity space. REBUILD may create a fresh identity space.

### WHERE

`type_handle`, `object_handle`, and `link_handle` identify physical/current locations inside one Graph representation.

They answer:

```text
Where is the current physical record?
```

They are not semantic identity and must not be required to survive physical relocation during normal BUILD.

### Current-location map

Type/object current location is a direct array indexed by semantic identity:

```text
current_location[identity_ref.slot()]
    -> current type/object physical location
    -> 0 when no current live entity exists
```

This is the existing `identity_ref -> Graph location` concept already present in V4 `graph::identity_locations` and persisted `graph_identity_index`.

The target keeps this direct-index property. It does not replace it with textual lookup, sorting, or a hash lookup.

## Persistent semantic references

A persisted semantic relationship must store WHO when the relationship is about semantic identity.

The first migration target is `type_ref::named`.

Current implementation:

```text
type_ref
[kind:2][payload:30]

named -> type_handle slot
```

Target:

```text
type_ref
[kind:2][payload:30]

intrinsic -> intrinsic_type
named     -> type identity_ref.slot()
derived   -> canonical derived-type slot
```

The representation remains four bytes.

Resolving a named type becomes:

```text
type_ref::named payload
    -> identity slot
    -> graph_identity_index[slot]
    -> current type_handle
```

This is one direct indexed current-location read and belongs to semantic, layout, materialization, or query preparation work. It is not a Runtime-cycle lookup.

Other persisted semantic references must be audited under the same rule.

Target migrations include:

```text
base_record::type
    type_handle -> identity_ref

object_endpoint::object
    object_handle -> identity_ref

construction_value::object_binding
    object_handle raw value -> identity_ref raw value
```

`member_record::type`, `object_entry::type`, endpoint-path types, and derived children become relocation-safe automatically when named `type_ref` becomes identity based.

The migration must be incremental and test-gated. Current APIs may continue to accept or return Graph handles after resolving semantic identity to the current location.

## Link identity

No new `identity_kind` is introduced for links.

A link already has a natural semantic ownership key:

```text
target object identity_ref
+ target endpoint_ref
```

The current link index therefore represents:

```text
semantic target endpoint -> current link_handle
```

When object endpoints become identity based, changing an object's physical location does not require rewriting all links that name that object.

## Append-only BUILD payload

Normal BUILD must not compact the whole Graph solely because one current entity changed.

For a changed semantic entity `A`:

```text
identity_ref(A)
    -> append replacement A-owned payload
    -> update current_location[A.slot]
```

The old physical payload becomes stale storage.

It is not:

```text
A v1
A v2
A v3
```

There is only one semantic `A`:

```text
identity_ref(A) -> current payload
```

Old physical bytes are storage garbage retained until a compaction boundary.

For deletion:

```text
current_location[A.slot] = 0
```

For addition:

```text
new identity_ref(E)
    -> append E payload
    -> current_location[E.slot] = new location
```

## Stale/history accounting

`history_count` must not mean semantic history.

The architectural meaning is:

```text
stale physical storage
```

Preferred terminology in implementation/docs is `stale_count` when practical.

For a physical arena:

```text
stale_count = physical_count - current_live_count
```

A count alone is not enough for compaction policy because one stale type may own many stale members/bases/construction records. Compaction policy should also be able to account for stale bytes or arena-specific stale payload.

No version chain is required.

No tombstone entity state is required merely to remember semantic WHO.

## BUILD

Target normal BUILD:

```text
persisted compiled/source/database state
    -> detect changed physical inputs
    -> affected physical closure
    -> affected semantic closure by identity_ref
    -> replay changed/affected semantic roots
    -> append replacement semantic payload
    -> patch direct current-location/index state
    -> persist changed/affected artifact payload
    -> publish
```

The target complexity is:

```text
O(changed + affected + persisted changed payload)
```

not:

```text
O(total Project size)
```

for an independent one-entity change.

`graph_delta` remains BUILD-operation state. It does not become resident G.

## REBUILD

REBUILD is the compaction boundary:

```text
current sources
    -> fresh semantic construction
    -> fresh compact G
    -> stale_count = 0
    -> fresh BUILD acceleration state
```

REBUILD may assign new physical Graph locations.

Because persisted semantic relationships are WHO based, relocation during REBUILD does not require preserving old physical handles.

REBUILD may also reconstruct `identity_ref` as already allowed by the V4 identity contract.

## compiled.bin

`compiled.bin` remains:

```text
final mmap-native semantic Project consumed by LOAD
```

It is not BUILD-cache state.

Target `compiled.bin` semantics:

```text
semantic identity space
current identity -> location index
semantic payload storage
current query indexes
Source Map / Assign and other final Project data
```

Normal BUILD may leave stale physical payload behind in the durable representation until REBUILD compacts it.

Stale payload must be unreachable from the current semantic indexes.

The storage mechanism for efficient candidate publication must avoid rewriting the complete artifact for an independent sparse change. The logical append/current architecture and the sparse physical persistence mechanism are separate implementation steps.

## source.bin

`source.bin` remains BUILD-only acceleration state.

Current V4 source semantic dependencies already persist exact `identity_ref` values:

```text
source_dependency_ref = exact identity_ref
```

This is the correct WHO contract and must remain independent of Graph-handle relocation.

`source.bin` v7 therefore provides a precedent for the target Graph direction: semantic dependency continuity is already identity based.

## Dense projection

Current V4 still uses `graph_dense_projection` before final BUILD persistence. It remaps Graph-local WHERE values into one dense final image.

That is incompatible with the final sparse-publication target because an independent change can force global physical remapping and full artifact materialization.

Removal order:

```text
1. migrate persisted semantic references from WHERE to WHO
2. make replacement payload append/current-index based
3. persist sparse changed payload
4. remove graph_dense_projection from normal BUILD
```

`graph_dense_projection` must not be removed before all semantic references that depend on physical relocation are audited.

## V3, current V4, target V4

V3 correctly established:

```text
identity_ref = WHO
Graph handle = WHERE
```

but preserved historical physical handles/tombstones across BUILD because persisted Graph relationships still depended on physical handles. Historical BUILD-only lookup indexes were kept outside READY query indexes.

Current V4 improved semantic dependency persistence:

```text
source.bin dependency -> exact identity_ref
```

and already has direct `identity_ref -> current Graph location`.

However persisted G still contains multiple WHERE-to-WHERE semantic references, and normal BUILD still finishes through dense projection/materialization.

Target V4 keeps the good V3 separation but completes it:

```text
semantic relationships -> WHO
current physical placement -> WHERE
normal BUILD -> append replacement + patch current location
REBUILD -> compaction
```

This avoids making stable physical handle lineage the semantic foundation.

## Performance evidence and target

The current sparse semantic machinery is already sparse for a one-type change, but work after `graph_delta` still grows with total Project size.

Observed current V4 timings from the sparse type benchmark:

| Types | Semantic BUILD | Dense projection | Artifact materialization |
|---:|---:|---:|---:|
| 1,000 | ~12.94 ms | ~0.049 ms | ~5.85 ms |
| 10,000 | ~32.45 ms | ~0.469 ms | ~17.58 ms |
| 100,000 | ~182.20 ms | ~4.94 ms | ~161.94 ms |

The dominant architectural target is therefore not merely deleting the projection loop. It is eliminating total-Project artifact materialization for a sparse BUILD.

Performance gates for an independent one-entity modification:

```text
semantic replay count
    proportional to affected roots/entities

Graph payload writes
    proportional to changed/affected payload

compiled.bin/source.bin persistence
    no required O(total G) materialization

Runtime construction
    must not scan stale physical payload
```

## Runtime / SHM boundary

Semantic G is identity based and relocatable across BUILD/REBUILD.

FIXED_DIRECT Runtime is address based and must remain stable after materialization.

The boundary is:

```text
semantic G
    identity_ref / WHO
        |
        v
Runtime construction
    resolve current locations once
    derive ABI layout
    derive object/member/base offsets
        |
        v
FIXED_DIRECT Runtime/SHM
    native objects
    native C++ references
```

There is no `identity_ref` lookup in the execution cycle.

Current Runtime construction is allowed to resolve identity to current Graph handles while the migration is in progress.

The next Runtime optimization gate is stricter:

```text
Runtime construction must scale with current semantic state,
not physical_count including stale BUILD payload.
```

A later incremental Runtime phase may use changed identities plus semantic dependency closure to rebuild only affected layout/materialization state.

Examples:

```text
initializer-only change
    ABI unchanged
    -> no complete layout rebuild required

record-layout change
    -> affected type closure
    -> affected objects/bindings
```

That later optimization must never weaken FIXED_DIRECT direct-reference execution.

## Implementation sequence

### GRAPH-IDENTITY-REF-01A

Migrate:

```text
type_ref::named
    type_handle slot -> identity_ref(type).slot()
```

Keep existing current-handle APIs by resolving through the direct identity location index.

Required regression:

```text
a named reference to A keeps the same 4-byte semantic value
when A's physical Graph location changes
```

### GRAPH-IDENTITY-REF-01B

Migrate:

```text
base_record::type
    type_handle -> identity_ref
```

Resolve to current `type_handle` at layout/materialization/query boundaries.

### GRAPH-IDENTITY-REF-01C

Migrate object semantic references:

```text
object_endpoint::object
construction_value::object_binding
```

to `identity_ref`.

Update link/initialization indexes so their semantic keys do not depend on object physical relocation.

### GRAPH-APPEND-01

Change normal BUILD replacement from stable-slot patch/reactivation to:

```text
append replacement payload
patch current_location
account stale storage
```

No semantic version chain.

### GRAPH-APPEND-02

Make `compiled.bin` and BUILD-only artifact publication physically sparse so an independent change does not require encoding/writing all unchanged payload.

### GRAPH-APPEND-03

Remove `graph_dense_projection` from normal BUILD.

REBUILD remains the compact/full-construction path.

### RUNTIME-CURRENT-01

Ensure `G -> runtime_layout -> FIXED_DIRECT` traverses only current semantic entities and does not scale with stale physical BUILD storage.

### RUNTIME-SPARSE-01

Only after current-only construction is correct and benchmarked, evaluate affected-closure incremental Runtime layout/materialization.

## Architectural gates

1. `identity_ref` is WHO. It never becomes a physical Graph slot.
2. Graph handles are WHERE. They are not semantic identity.
3. Persisted semantic relationships use WHO where relocation must be transparent.
4. Runtime-cycle execution never performs identity/name/hash lookup.
5. Normal BUILD does not compact the complete Graph.
6. REBUILD is the compaction boundary.
7. Stale physical payload is storage garbage, not semantic history.
8. No `A v1/A v2/A v3` semantic version model is introduced.
9. No new link `identity_kind` is introduced merely to preserve link location.
10. Sparse semantic BUILD is not considered complete while durable publication still requires O(total G) materialization.
11. Runtime construction must never iterate stale physical payload merely because BUILD retained it for storage efficiency.
12. Do not reintroduce V3 historical-handle/tombstone lineage as V4's primary semantic identity mechanism.
