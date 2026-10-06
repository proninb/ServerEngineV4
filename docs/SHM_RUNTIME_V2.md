# SHM Runtime V2

This branch builds a second Runtime/SHM path from zero while the existing
Runtime remains the correctness/performance baseline.

```text
G mmap
  -> SHM layout
       type slot -> size/alignment
       global member -> record offset
       global base -> record offset
       object slot -> SHM offset
       unconnected<T> -> SHM offset
  -> SHM materialization (next slice)
```

Core model:

```text
Object -> Type -> Members -> SHM
```

Rules:

- no map / unordered_map;
- no name/hash lookup in the SHM path;
- `identity_ref` is semantic WHO;
- `graph_identity_index[identity.slot()]` is the direct WHO -> WHERE transition;
- all working data is dense slot/range indexed;
- no copy of G and no second semantic representation;
- no dependency on `runtime_layout`, `runtime_type_plan`, Runtime program
  micro-ISA, or `fixed_direct_materializer`.

`SHM-LAYOUT-01` only derives physical layout. It is intentionally not wired
into Project LOAD yet. The next gate is an old-vs-new layout comparison before
any SHM bytes are written.


## SHM-OBJECTS-01

The next layer writes the fixed SHM directly. There is still no construction
plan and no instruction program:

```text
mmap-native G
    +
dense shm_layout
    |
    v
Object
    -> Type
        -> Bases
        -> Members
            -> scalar write / nested record / array / native reference
    |
    v
fixed SHM
```

The implementation has no materialization workspace, map, hash table, name
lookup, or copied Runtime image.

Reference construction is resolved directly from persisted construction
semantics. `member_binding` follows type-local member indices and dense member
offsets. Reference chains are bounded by the owning record's member count and
need no sidecar. `object_binding` uses the persisted graph identity index
directly.

This slice contains two direct phases:

1. canonical `unconnected<T>` reference initialization;
2. Project Object -> Type -> Members construction.

Constructor-default records, static links and per-object initializations remain
separate future phases. The benchmark uses a real zeroed `fixed_shared_memory`
mapping at the configured FIXED_DIRECT address; there is no heap Runtime image
and no copy into SHM.


## SHM-NAMED-ALIAS-01

`type_ref_kind::named` may address either a record or an intrinsic alias.
Direct SHM construction resolves the dense type slot and dispatches by
`type_entry.kind`:

```text
named
  -> intrinsic_alias -> intrinsic physical write/no-op
  -> record          -> Bases -> Members traversal
```

No plan, map, lookup table, or copied Runtime representation is introduced.


## SHM-TOP-REFERENCE-01

A reference is a complete `type_ref` and is not restricted to a record member.
A top-level Project Object may therefore have type `T&` or `T&&`.

For zero/default construction, the Object reference word is written directly
with the native address of canonical `unconnected<T>` storage in the same fixed SHM.
No plan, sidecar, map, or copied Runtime representation is introduced.


## SHM-SPARSE-TYPES-01

The direct `Object -> Type -> Members` benchmark is the correctness oracle, but
it is not the Runtime execution model. UnitProXL showed hundreds of millions of
repeated structural visits and more than 311 million zero no-ops.

Sparse preparation now compiles G + `shm_layout` once into physical work only:

```text
mmap-native G + shm_layout
        |
        v
actions[] + canonical_roots[] + object_roots[]
        |
        v
fixed SHM
```

The retained action set is deliberately small:

```text
reference_relative
reference_absolute
store
call
repeat
```

There is no zero action. Reference chains are resolved during preparation.
The executor does not read G, type/member/construction records, names, maps, or
lookup tables. `CALL` and `REPEAT` preserve structural reuse instead of
flattening every nested instance or array element.

Constructor defaults, static links, and per-object initializations remain
separate later phases.


## SHM-SPARSE-COMPACT-01

The first sparse representation proved the physical model but used a 48-byte
hot action. The compact representation keeps the same action semantics and
moves only wide payloads into cold arrays.

```text
hot action = 16 bytes

target : u32
arg0   : u32
arg1   : u32
kind   : u8
aux    : u8
flags  : u16
```

Cold payloads are retained only where the 16-byte instruction cannot carry the
physical data directly:

```text
absolute_offsets[]   : shm_offset
constants[]          : 16-byte scalar payload
repeat_descriptors[] : child range + stride + count
```

Encoding:

```text
reference_relative -> arg0 = source record offset
reference_absolute -> arg0 = absolute_offsets index
store              -> arg0 = constants index, aux = byte size
call               -> arg0 = child begin, arg1 = child count
repeat             -> arg0 = repeat_descriptors index
```

This step does not add fusion or change construction semantics. The executor
still performs the same physical writes and the benchmark retains exact
reference/store write-count checks.


## SHM-SPARSE-PRETOUCH-01

The benchmark now executes the same prepared compact sparse image twice.
The first run uses a fresh zeroed FIXED_DIRECT mapping. The second run
recreates the mapping, writes one zero byte to every OS page, then runs
the same sparse canonical and object executors.

`fixed_shared_memory::size_alignment()` is the pretouch stride; on Windows
this is `SYSTEM_INFO::dwPageSize`. Pretouch time is reported separately
from sparse execution time. The second run must reproduce identical
reference writes, scalar writes, and action visits. No sparse compiler or
executor semantics are changed by this measurement slice.


## SHM-SPARSE-CALL-PROFILE-01

CALL optimization is gated by measurement rather than by copying the old
Runtime fusion policy.

The profiler analyzes the already prepared physical sparse graph; it does not
instrument the timed executor and performs no SHM writes. Root execution counts
are propagated analytically:

```text
root       -> child executions += 1
CALL       -> child executions += parent executions
REPEAT N   -> child executions += parent executions * N
```

Sparse child programs are emitted before their parents, so program starts are
processed in descending action order. This produces exact Runtime CALL visit
counts without executing every Runtime instance.

CALL children are reported by direct action count:

```text
1
2
3
4
5..8
9..16
>16
```

and by direct shape:

```text
leaf              no CALL and no REPEAT in child
contains CALL
contains REPEAT
```

Both stored CALL actions and Runtime CALL visits are reported. Leaf counts are
also broken down by size bucket so a future `SHM-SPARSE-FLAT-LEAF` threshold is
selected from measured execution weight and retained-image growth.


## SHM-TYPE-AREA-01

This slice tests a different physical Runtime architecture in parallel with the
accepted sparse path.

`identity_ref` remains the canonical semantic WHO, but it is intentionally
consumed only at the semantic -> physical prepare boundary:

```text
identity_ref
    -> direct compiled G WHO -> WHERE
    -> dense physical type/object slot
```

No `identity_ref` survives into the Type Area hot executor.

Default Runtime discovery is object-driven:

```text
for each live Object:
    ensure its Type API
    publish Object slot -> SHM WHERE
```

Type dependencies are prepared once. Nested record and base APIs are flattened
once into their owning Type API; there is no generic CALL instruction and no
mixed opcode interpreter.

The retained physical area is separated by operation kind:

```text
TypeApi[]
RelativeRef[]
AbsoluteRef[]
ObjectRef[]
Store[]
Repeat[]
ObjectWhere[]
ObjectRuntime[]
```

`ObjectRef` stores an already-resolved `object_slot`, not `identity_ref` and
not an absolute source address. The hot materializer performs only:

```text
object_slot -> ObjectWhere[slot] -> SHM address
```

This preserves the semantic/physical boundary while allowing object placement
to change without resolving the semantic WHO again.

`Repeat` is retained only for bounded arrays whose non-zero physical child API
must execute repeatedly. Record/base composition does not use Repeat or CALL.

Canonical `unconnected<T>` APIs are prepared after object-driven default Type
discovery because canonical storage can introduce additional Runtime-only type
requirements.

API hashing and incremental Type Area reuse are intentionally out of scope for
this slice. G remains semantic truth; Type Area is disposable physical Runtime
state.

The benchmark keeps the sparse path unchanged and measures Type Area on a
separately recreated pretouched FIXED_DIRECT mapping. It requires exact
agreement with the direct correctness oracle for:

```text
all reference writes
relative/member-binding writes
absolute/unconnected writes
object-binding writes
scalar writes
live object count
```


## SHM-TYPE-AREA-FINALIZE-01

The first Type Area prototype proved the hot execution model but retained
builder-only flattened APIs. On UnitProXL that produced about 420 MB of
resident physical metadata.

The builder is still allowed to create expanded intermediate APIs. Before the
Type Area is published, a finalization pass now derives the exact Runtime root
set:

```text
ObjectRuntime.type_api
CanonicalRoot.type_api
```

and follows only retained `Repeat.type_api` dependencies. Record/base
composition is already flattened and therefore creates no final API edge.

Finalization performs:

```text
mark reachable TypeApi
    -> build old API -> final API remap
    -> compact operation ranges
    -> remap Repeat.type_api
    -> compact referenced constants
    -> remap ObjectRuntime / CanonicalRoot API ids
    -> discard builder-only arrays
```

The hot executor is unchanged. No G access, semantic identity, hash lookup,
generic CALL, or mixed opcode stream is introduced.

This slice deliberately does not yet optimize the cost of constructing the
temporary expanded builder image. Its first purpose is to measure how much of
the 420 MB was builder-only state while preserving the ~115 ms Type Area
execution shape. A later construction slice may avoid generating discarded
intermediate payload in the first place.


## SHM-TYPE-BATCH-01

The flattened Type Area experiment proved the target hot execution cost
(~115 ms object materialization on the UnitProXL workload), but expanded
nested Type APIs to roughly 420 MB. FINALIZE-01 showed that this was not
unreachable builder state: the duplication lived inside reachable parent APIs.

TYPE-BATCH-01 changes the representation, not the physical semantics.

Each Type API now retains only its local physical work:

```text
TypeApi[]
    local RelativeRef range
    local AbsoluteRef range
    local ObjectRef range
    local Store range
    Child range
    Repeat range
```

A nested record/base is represented once:

```text
Child {
    target_offset,
    child_type_api
}
```

There is no flattened copy and no generic CALL opcode.

The prepare boundary remains:

```text
identity_ref WHO
    -> resolve once in G
    -> type/object physical slot
```

No semantic identity survives into the hot executor. `object_binding` is
resolved during prepare to `object_slot`; execution uses only:

```text
object_slot -> ObjectWhere[] -> SHM WHERE
```

After all local Type APIs and Object WHERE values are known, object roots and
canonical roots are grouped by final Type API. The executor processes each
group in fixed batches of 64 WHERE values:

```text
for Type group:
    for batch[<=64]:
        apply local API to all roots
        traverse Child edges once for the batch
        traverse Repeat edges once for the batch
```

Therefore structural type traversal is amortized across a batch instead of
occurring once per object. Object-local memory writes remain exact and cannot
be eliminated.

This slice intentionally keeps object-specific construction as a separate
patch layer and keeps bounded arrays as Repeat edges. No hash, API interning,
generic VM, map, name lookup, or G traversal exists in the timed executor.

The benchmark runs TYPE-BATCH on a separately recreated, pretouched
FIXED_DIRECT mapping and requires exact agreement with the direct oracle for
all reference classes, stores, and live object count.


## SHM-HYBRID-PROFILE-01

TYPE-BATCH-01 established the compact local Type representation (~8 MB) but
did not improve execution over the sparse interpreter on UnitProXL. The fully
flattened Type Area established the opposite point: ~115 ms execution at
roughly 420 MB.

HYBRID-PROFILE-01 is measurement-only. It does not alter any materializer.

The profiler consumes only the prepared `shm_type_batch` image. For every
object-root Type API it computes the exact record/base flattenable closure:

```text
local physical operations
+ all Child subtrees
```

`Repeat` targets are intentionally not recursively flattened. Arrays remain
explicit Repeat edges, matching the proven Type Area architecture. Constants,
ObjectWhere, and other shared Runtime state are not charged again.

For each object-root Type API the profiler derives:

```text
flat_bytes
object_root_count
flattenable_child_visits_per_root

weighted_benefit =
    object_root_count * flattenable_child_visits_per_root

current_batch_benefit =
    ceil(object_root_count / 64) * flattenable_child_visits_per_root
```

Candidate APIs are ranked by `weighted_benefit / flat_bytes`. The benchmark
reports greedy cumulative Pareto points for additional flat-memory budgets of:

```text
+1 MiB
+4 MiB
+8 MiB
+16 MiB
+32 MiB
+64 MiB
```

Each point reports selected Type API count, bytes consumed, covered object
roots, weighted Child visits removable by a future object-major flat path, and
Child visits removable under the current batch execution policy.

This slice exists only to choose a selective flatten policy from measured
UnitProXL data. It introduces no hash, map, G lookup, identity lookup, or hot
execution branch.


## SHM-HYBRID-04M-01

HYBRID-04M is the first execution experiment that combines the compact local
Type Batch representation with a bounded object-major flat cache.

The base Type Batch remains authoritative physical Runtime metadata:

```text
TypeBatch
    TypeApi[]
    Child[]
    Repeat[]
    ObjectWhere[]
    ObjectRuntime[]
```

Hybrid is strictly additive and purely physical. It is prepared from the
already-built Type Batch image and never reads G, names, `identity_ref`, or
semantic state.

The hot cache budget is 4 MiB of flat payload. Selection uses the measured
object-major benefit/cost model:

```text
benefit = object_roots * flattenable Child visits
cost    = flat physical API bytes
score   = benefit / cost
```

Candidates are greedily selected by descending score under the fixed payload
budget. The dense local-TypeApi -> flat-TypeApi map is small fixed overhead and
is reported separately from the 4 MiB payload.

A selected flat API recursively expands record/base `Child` edges exactly once:

```text
FlatApi[]
FlatRelativeRef[]
FlatAbsoluteRef[]
FlatObjectRef[]
FlatStore[]
FlatRepeat[]
```

`FlatRepeat` deliberately points back to a local Type Batch API. Bounded arrays
were excluded from the flatten benefit model and remain structural; this keeps
the selective budget aligned with the profile and avoids silently expanding
large arrays.

Object execution is split before hot work:

```text
HOT selected roots:
    original ObjectRuntime order
    -> object-major flat execution

COLD roots:
    existing Type Batch groups
    -> local batched execution

Object-specific patches:
    applied once after both paths
```

The Hybrid area does not duplicate Type Batch constants, ObjectWhere, objects,
or cold root groups.

The benchmark uses a separate pretouched FIXED_DIRECT mapping. Canonical
construction stays on the compact Type Batch path. Object construction is
validated independently against the direct oracle for member-binding,
unconnected, object-binding, store and object counts. The UnitProXL workload's
current `object_binding == 0` remains only a workload-specific observation;
the Hybrid path implements object bindings generically through the already
resolved object slot and `ObjectWhere[]`.


## SHM-HYBRID-04M-WORK-01

The first 4 MiB Hybrid selector ranked candidates only by:

```text
roots * flattenable_child_visits / flat_bytes
```

The measured Hybrid run reduced object materialization from the compact
Type-Batch level (~226 ms) to ~189 ms while the remaining local child-visit
count stayed high. This shows that the hot flat path gains from both structural
elimination and object-major physical-write locality.

WORK-01 keeps the representation, 4 MiB budget, prepare boundary, and executor
unchanged. Only candidate ranking changes.

For one candidate:

```text
physical_per_root =
    flat_relative_refs
  + flat_absolute_refs
  + flat_object_refs
  + flat_stores

weighted_physical_writes =
    roots * physical_per_root

weighted_work =
    roots * flattenable_child_visits
  + weighted_physical_writes

score =
    weighted_work / flat_bytes
```

Repeats remain structural and are deliberately not credited as flat physical
writes because the Hybrid flat representation still dispatches their child
through the compact local Type API.

This is an A/B selection-policy experiment at the same exact memory budget.
No new Runtime representation, semantic lookup, hash, map, or hot-path branch
is introduced.


## SHM-HYBRID-BUDGET-SWEEP-01

The 4 MiB Hybrid run proved that selective object-major flattening is useful,
but one point cannot establish the memory/performance knee.

This slice does not change Hybrid representation, WORK-01 ranking, or the hot
executor. It only parameterizes the flat payload budget used by prepare:

```text
prepare_shm_hybrid_budget(base, budget_bytes, ...)
```

The existing `prepare_shm_hybrid_04m()` remains a compatibility wrapper for
exactly 4 MiB.

The benchmark measures the same physical Hybrid architecture at:

```text
1 MiB
2 MiB
4 MiB   (existing path)
8 MiB
16 MiB
32 MiB
```

Every point uses a separately recreated and pretouched FIXED_DIRECT mapping.
Canonical construction still uses the compact Type Batch; only Project object
materialization is Hybrid.

The sweep validates the exact physical write counts against the direct oracle
for every additional budget point. This produces a real runtime
memory/performance curve rather than extrapolating from the profile model.


## SHM-TYPE-INLINE-08-01

The hybrid budget sweep established that full root flattening reaches a sharp
knee around 4 MiB but still leaves object materialization around ~190 ms.
Increasing the hot root-flat budget to 32 MiB yields only a small additional
gain. Therefore the next experiment targets the remaining structural cost
inside the compact Type API itself rather than spending more root-flat memory.

TYPE-INLINE-08 is a second prepare policy for the existing `shm_type_batch`
representation and uses the exact same hot executor.

When a child Type API is:

```text
leaf:
    children == 0
    repeats == 0

small:
    relative_refs
  + absolute_refs
  + object_refs
  + stores
  <= 8
```

the child edge is removed and those local physical operations are copied into
the parent with the child record offset applied.

Because dependency APIs are prepared before parents, this policy naturally
cascades bottom-up: after its own small leaf children are fused, a type may
itself become a small leaf and can then be fused into its parent.

Arrays remain structural because any API containing `Repeat` is excluded.
There is no generic CALL, VM, hash, map, name lookup, or semantic identity in
the executor. `object_binding` remains resolved during prepare to object slot.

TYPE-INLINE-08 is built as an independent `shm_type_batch`, so its
`resident_bytes` is the actual standalone Runtime metadata cost, not a delta
kept beside the baseline Type Batch.

The benchmark executes the same `materialize_shm_type_batch_*` functions for
both baseline and INLINE-08 images and requires exact physical-write equality
against the direct construction oracle.


## SHM-TYPE-INLINE-08-OBJECT-MAJOR-01

This is an execution-order A/B experiment over exactly the same INLINE-08
physical representation.

The existing INLINE-08 executor is type-group/batch ordered:

```text
Type group
    -> batch roots
    -> API tree
    -> writes across roots
```

The object-major measurement path is:

```text
Object
    -> complete API tree
    -> all physical writes for that Object
    -> next Object
```

No Type API representation, inline policy, semantic resolution, object
placement, or physical write semantics changes. Canonical construction remains
on the existing batch path.

The purpose is to isolate SHM write/cache locality from structural traversal
cost. The hot object-major executor still has no G/project access,
identity_ref, hash/map/name lookup, or generic opcode VM.


## SHM-TYPE-SUBTREE-INLINE-08-01

INLINE-08 proved that object-major execution over a partially inlined local
Type Area is materially faster than type-group/batch execution over the same
physical representation.

Leaf-only INLINE-08 can fuse a leaf child with at most 8 physical writes. It
cannot fuse a small structural chain when an intermediate wrapper still owns a
Child edge.

SUBTREE-INLINE-08 therefore inlines one complete Child subtree when the entire
subtree:

- contains no Repeat edge;
- expands to at most 8 physical writes;
- is dependency-first and acyclic.

All copied operations are rebased to the parent record offset. The hot executor
is unchanged and remains object-major. No semantic identity, G access,
hash/map/name lookup, generic CALL, or opcode VM is introduced.


## SHM-TYPE-INLINE-SWEEP-01

INLINE-08 object-major and SUBTREE-INLINE-08 converged on essentially the same
physical image and execution time. Bottom-up leaf inlining therefore already
captures the useful <=8 subtree closure for the measured workload.

This slice does not add another representation. It sweeps the same bottom-up
leaf-inline policy at physical-operation thresholds:

```text
8   existing reference point
16
32
64
```

All variants use the already accepted object-major executor:

```text
Object
    -> complete prepared Type API tree
    -> next Object
```

No semantic G access, identity_ref, hash/map/name lookup, generic CALL VM, or
new Runtime addressing model is introduced. The experiment measures the real
resident-memory / remaining-child-traversal / materialization-time Pareto curve
between compact INLINE-08 and the full flattened Type Area.


## SHM-TYPE-INLINE-64-FINAL-01

The INLINE threshold sweep established the current Runtime V2 production
candidate:

```text
prepare policy:  inline leaf <= 64 physical operations
hot object path: object-major
```

On the UnitProXL workload this retained about 41.45 MB of Type metadata and
reduced object materialization to about 127 ms, compared with about 234 ms for
the compact structural Type Batch and about 121 ms for the 420 MB fully
flattened Type Area.

The production-candidate API is now isolated as:

```text
shm_runtime_v2
prepare_shm_runtime_v2()
materialize_shm_runtime_v2_canonical()
materialize_shm_runtime_v2_objects()
```

`shm_runtime_v2` deliberately exposes no threshold/configuration knob.
`64` is an implementation policy selected from measurement, not project
semantics or user configuration.

Internally the candidate reuses the validated Type Batch representation:

```text
prepare_shm_runtime_v2
    -> INLINE-64 prepare
    -> local TypeApi + selectively inlined leaf physical work

materialize_shm_runtime_v2_objects
    -> object-major executor
```

Experimental `local`, `INLINE-08/16/32`, SUBTREE-08, batch execution, Hybrid,
and full Type Area remain benchmark/reference paths. They are not part of the
candidate Runtime API.

The semantic/physical contract remains:

```text
identity_ref = semantic WHO
prepare      = resolve WHO once -> dense physical slot/offset
hot runtime  = TypeApi/ObjectWhere/offset only
```

No `identity_ref`, G traversal, hash/map/name lookup, generic CALL VM, or
runtime policy branch is introduced into the hot object executor.

### Integration boundary

This slice does **not** replace `create_resident_project()` yet.

The existing resident Project path publishes `runtime_binding_index` from the
old `runtime_layout`. Runtime Query / IC addressing therefore still depends on
that construction pipeline. Replacing only the SHM materializer would create
two physical Runtime descriptions and violate the one-Runtime-state goal.

The next production integration slice must construct the resident
`runtime_binding_index` directly from Runtime V2 physical data
(`shm_layout` / Object WHERE / member offsets), then wire
`create_resident_project()` to the single V2 path and discard construction-only
V2 metadata after publication where possible.


## RUNTIME-V2-RESIDENT-01

`INLINE-64 + object-major` is now wired into the resident Project publication
bridge as the first production Runtime V2 path.

For a V2-complete image the lifecycle is:

```text
mmap compiled.bin
    |
    v
prepare shm_layout
    |
    v
prepare Runtime V2 (INLINE-64)
    |
    v
create + pretouch FIXED_DIRECT SHM
    |
    +-> V2 canonical materialization
    +-> V2 object-major materialization
    |
    v
publish runtime_binding_index directly from shm_layout
    |
    v
resident Project
```

The resident binding sidecar is derived from the same `shm_layout`; a second
`runtime_layout` is not constructed for that publication.

This slice deliberately does **not** claim semantics that Runtime V2 has not
implemented yet. The old materializer also owns three later construction
phases:

- constructor-default records;
- static Graph links;
- per-object initializations.

If any of those are present, publication selects the legacy compatibility path
for the whole Runtime. The two physical layouts are never constructed together
for one publication. Telemetry reports each blocker explicitly.

This is a forward migration boundary, not the final dual-path architecture.
The next resident slice must move those three cold phases onto `shm_layout` /
Runtime V2, validate old-vs-new final SHM bytes, then remove the compatibility
gate and old construction path from Project publication.

Stable-WHERE object holes are valid in `shm_layout`: retired object slots retain
their Graph WHERE lineage but receive no Runtime storage and cannot resolve
through the resident binding index.


## RUNTIME-V2-CONSTRUCTOR-02

Constructor-default records are now part of Runtime V2.

They are not emitted into the ordinary Type API `stores` range. A constructor
override is semantically later than base/member construction and may overwrite
a value produced by a child Type API, including an explicit zero override.
Each Type API therefore has a separate `post_stores` range:

```text
Type API
    references / ordinary stores
    -> child/base APIs
    -> repeats
    -> post_stores       // constructor defaults
```

Persisted constructor paths are resolved during V2 prepare using the same
`constructor_path_reader` grammar used by the legacy materializer. The final
Runtime metadata contains only numeric record offsets and scalar constants.
No path strings, names, hashes, semantic identities, or Graph lookup occur in
the hot executor.

A constructor-bearing Type API is deliberately excluded from leaf/subtree
inlining in this slice. This preserves ordering without adding a VM or a new
flattened per-object patch representation. Ordinary constructor-free types keep
the accepted INLINE-64 behavior unchanged.

The resident compatibility gate is reduced to:

```text
static Graph links
per-object initializations
```

Constructor defaults no longer force legacy Runtime publication.

Experimental Hybrid paths explicitly reject a constructor-bearing Type API
until their flat representation gets an ordered post-store phase. They must
never silently reorder constructor overrides.


## RUNTIME-V2-LINKS-03

Static Graph links are now a native Runtime V2 construction phase.

The legacy semantic ordering is preserved:

```text
canonical
    -> mark link target reference slots as pending
    -> object-major INLINE-64 construction
    -> resolve/materialize links
    -> per-object initializations
```

The mark phase is required. A normal reference initializer must not overwrite a
slot owned by a static link. Runtime V2 reference writes therefore recognize
the reserved `~link_handle` pending marker and leave that target slot untouched.

### Physical endpoint representation

Graph endpoint metadata is consumed only during Runtime V2 prepare. Member,
base, and array-index displacements between dereferences are folded into one
physical displacement. Runtime stores only:

```text
endpoint_program
    root object WHERE
    dereference range
    final static tail
    final-is-reference

dereference[]
    static displacement before native reference load
```

There are no names, identity lookups, member lookups, path handles, or Graph
walks in link execution.

### Link dependency resolution

After object construction, ordinary member/reference construction has already
resolved to native Runtime addresses. Link resolution therefore only needs to:

1. execute the prepared physical source endpoint;
2. follow native references;
3. if a reference contains another pending link marker, resolve that link by
   its stable Graph link WHERE;
4. write the final native source address into the target slot.

Each stable Graph link slot owns one `link_plan`. Dead Graph link slots remain
empty so `~link_handle` remains a direct one-based index into physical link
state.

A link dependency cycle is detected by the physical
`marked -> resolving -> resolved` state transition and fails closed.

The resident V2 compatibility gate is now reduced to only:

```text
per-object initializations
```

Static links no longer require `runtime_layout` or the legacy materializer.


## RUNTIME-V2-INITIALIZATIONS-04

Source scalar initializations are now the final native Runtime V2 construction
phase.

The complete semantic construction order is:

```text
canonical
    -> mark static-link targets
    -> object-major INLINE-64 construction
       -> constructor post_stores
    -> resolve/materialize static links
    -> Source scalar initializations
```

Graph initialization metadata is consumed during V2 prepare. Each persisted
initialization becomes one compact physical plan:

```text
initialization_plan
    endpoint_program target
    scalar bytes[<=16]
    scalar size
```

The endpoint program is shared with the static-link implementation. Member,
base and array-index segments are folded into physical offsets; dereference
steps remain only where the final target depends on a native Runtime reference.

Initialization execution therefore performs no Graph/name/hash/member lookup.
It follows already-materialized native references, computes one target address,
and copies one scalar payload.

The scalar codec preserves the legacy FIXED_DIRECT rules for bool/integer/real,
nullptr and pointer-zero construction. Reference targets, record targets,
arrays, const-qualified targets and binding constructions fail closed.

`create_resident_project()` no longer has a semantic compatibility condition:
Runtime V2 is selected for projects containing constructor defaults, static
links and Source scalar initializations. The legacy branch is intentionally
left source-visible for this validation checkpoint only and is no longer
selectable. After this slice passes, RUNTIME-V2-ONLY-05 removes that dead branch
and its production dependency on runtime_layout/fixed_direct_materializer.


## RUNTIME-V2-ONLY-05

Resident Runtime publication is Runtime V2 only.

The production path is now:

```text
mmap compiled.bin / G
        |
        v
    shm_layout
        |
        v
 Runtime V2 prepare
        |
        v
 FIXED_DIRECT SHM
   canonical
   mark links
   object-major construction
   constructor post_stores
   materialize links
   Source initializations
        |
        v
 runtime_binding_index
        |
        v
 resident Project
```

There is no `use_runtime_v2` selector and no production fallback to
`runtime_layout` / `fixed_direct_materializer`.

`runtime_binding_index` has been split into `runtime_binding.hpp/.cpp`. It is a
resident ABI address sidecar for Runtime Query and IC, not construction state.
Production `project`, Runtime Query, and Runtime IC therefore no longer include
`runtime_layout.hpp`.

The production Server target no longer compiles:

```text
runtime_layout.cpp
fixed_direct_materializer.cpp
```

Those files remain in the repository and in `ServerEngineV4Tests` only as a
reference/differential implementation while Runtime V2 stabilization continues.

The legacy `load-profile` interface was removed because its counters described
the old fixed_direct materializer rather than the production Runtime V2 path.
The current benchmark-only `load-profile` mode reports Runtime V2 telemetry;
ordinary `load` runs without telemetry, matching the Server path.


## RUNTIME-V2-AUDIT-06

The Runtime V2 production boundary was audited after V2 became the only
resident materializer.

### Resident state

`project` owns only:

```text
project_path
compiled.bin read-only mapping
compiled_project_view
FIXED_DIRECT shared memory
runtime_binding_index
runtime_size
IC catalog state
```

`shm_layout` and `shm_runtime_v2` are construction-local objects in
`create_resident_project()` and never enter `project`.

The INLINE-64 Type API, physical link plans, initialization plans and endpoint
dereference programs are therefore transient construction metadata.

After `runtime_binding_index` has been built, Runtime V2 now explicitly clears
both `shm_runtime_v2` and `shm_layout` before allocating the resident `project`.
This makes the construction/resident lifetime boundary physical rather than
merely relying on function-scope destruction.

`runtime_v2_metadata_bytes` remains a telemetry field for compatibility, but it
measures peak transient construction metadata, not resident Project memory.

### Production build boundary

The production `ServerEngineV4` target now contains only the accepted SHM path:

```text
shm_layout
shm_type_batch
shm_runtime_v2
runtime_binding
```

The following older/experimental implementations are no longer compiled into
the production Server target:

```text
shm_materializer
shm_sparse
shm_type_area
shm_hybrid_profile
shm_hybrid_04m
runtime_layout
fixed_direct_materializer
```

The five SHM comparison implementations remain compiled explicitly by
`ServerEngineV4ShmLayoutBenchmark`. `runtime_layout` and
`fixed_direct_materializer` remain explicit dependencies only of the legacy
reference benchmarks/tests.

This keeps benchmark/reference code available without allowing it to become an
accidental production dependency.


## RUNTIME-V2-BENCH-07

The existing Runtime fixture generator now supports a final Runtime V2 kernel
mode:

```text
ServerEngineV4RuntimeBenchmark <scenario> <count> v2
```

It uses exactly the same encoded `compiled_project_view` fixtures as the legacy
kernel benchmark:

```text
objects
links
indexed_links
chain
many_types
```

The V2 mode measures the accepted construction stages independently:

```text
shm_layout
Runtime V2 prepare
SHM create
canonical
link target mark
object-major construction
link materialization
Source initializations
```

It also reports transient `construction_bytes`, INLINE-64 Type API shape and
execution counters.

This is deliberately a kernel A/B benchmark. It does not copy the production
parallel pre-touch implementation into benchmark code. End-to-end resident
publication, including production parallel pre-touch and binding-index
publication, remains measured by `ServerEngineV4PublishBenchmark load`.

The old two-argument `ServerEngineV4RuntimeBenchmark` mode remains the legacy
reference kernel. Its output is now labelled `benchmark=legacy_kernel`.

`benchmarks/run_runtime_v2_scale.ps1` runs the same scale matrix previously used
by the legacy runtime benchmark and writes a new CSV. Existing
`run_runtime_scale.ps1` is intentionally unchanged so historical legacy results
remain reproducible.


### BENCH-07 indexed-array endpoint correction

The final V2 scale benchmark exposed a missing physical-layout resolution case
for endpoint paths whose `array_index` traverses an array of a named record.

`shm_layout::value(type_ref)` intentionally does not resolve `named` type_refs:
their payload is semantic identity, not `type_handle`. Runtime V2 link prepare
was incorrectly using that API to obtain the array element stride.

The endpoint compiler now resolves named array elements as:

```text
type_ref(named)
    -> compiled_project_view::named(...)
    -> type_handle
    -> shm_layout::type(...)
```

Non-named element types still use `shm_layout::value(...)`.

The existing indexed-subobject fixture now executes both the legacy reference
materializer and Runtime V2, and verifies that the final target native reference
points at the same indexed source element.


## RUNTIME-IMAGE-08A

`compiled.bin` format v21 adds the final `runtime_abi_layout` section.
PUBLISH and REBUILD derive the configured target-ABI SHM layout once and encode
it directly into the same writable `compiled.bin` mmap. Only offsets, sizes and
alignments are persisted; no native process pointer or SHM base address is stored.

LOAD uses the persisted section when its ABI/pack and semantic cardinalities
match, skipping `prepare_shm_layout()`. Benchmark output reports
`runtime_v2_persisted_layout=1` for this path.

A sparse BUILD mutation currently invalidates the section magic. LOAD then
falls back to deriving layout from G. This preserves correctness until the
following sparse Runtime-image slice patches the affected physical layout
records directly.

This is the first vertical cut toward:

```text
PUBLISH / REBUILD
    G -> ABI layout -> Runtime construction representation -> compiled.bin

LOAD
    mmap -> allocate SHM -> physical replay
```

08A moves ABI layout. The following slice persists the Runtime V2 construction
representation itself.


## COMPILED-PHYSICAL-COLUMNS-09B-01

Variant C extends the shared Graph WHERE-space to immutable Runtime execution
columns:

```text
links[N]                  <-> link_runtime[N]
object_initializations[N] <-> initialization_runtime[N]
```

Both common records are fixed 32-byte mmap-native columns. LOAD performs no
copy/deserialization of these plans and introduces no Runtime identity space.

Only endpoints containing an actual reference dereference allocate exception
storage:

```text
runtime_endpoint_programs[]
runtime_endpoint_dereferences[]
```

Their exact counts are computed from semantic endpoint paths before
`compiled.bin` is created; no worst-case capacity is reserved.

PUBLISH/REBUILD compile the link/initialization physical columns once after ABI
columns are available. LOAD still prepares the INLINE-64 Type area, but binds
links and initializations directly from mmap. The only per-LOAD mutable link
metadata is `link_state[]`, required for dependency/cycle resolution.

A semantic BUILD mutation invalidates both physical headers:

```text
runtime_abi_header
runtime_execution_header
```

so BUILD remains correct while sparse physical-column patching is implemented
later.

The next slice moves Type/INLINE-64 operation ranges into the same physical
column model.


## PUBLISH-LOAD-DIRECT-09B-02

The current production boundary is:

```text
PUBLISH
    compile semantic G
    -> compile physical ABI / Runtime columns
    -> write compiled.bin

LOAD
    mmap compiled.bin
    -> trusted non-owning views
    -> allocate SHM
    -> execute
```

`compiled.bin` is an internal ServerEngine image with one current schema.
Normal LOAD therefore does not perform file-format or physical-image
validation:

```text
no magic/version/endian checks
no CRC checks
no file-size/directory-shape validation
no record-size validation
no physical-record scans
no semantic Runtime fallback
no link/initialization recompilation
```

`compiled_project_view::bind()` and `verify_contents()` remain cold
PUBLISH/test utilities. `load_project()` uses `compiled_project_view::attach()`.

The former LOAD-side probe/validation APIs are removed from production:

```text
shm_layout_columns_available()
bind_shm_layout_columns()
shm_runtime_v2_physical_columns_available()
bind_shm_runtime_v2_physical_columns()
```

LOAD directly attaches persisted ABI and Runtime execution spans. The Runtime
execution attachment allocates only the mutable link-resolution state needed
while constructing SHM.

BUILD is intentionally outside this slice. BUILD is a separate incremental
pipeline using its persisted build state; it is not a fallback mechanism for
LOAD.

The remaining compiler work on LOAD is INLINE-64 Type preparation. Persisting
that Type execution image is the next Runtime slice.


### 09B-02 API boundary correction

`prepare_shm_runtime_v2()` remains the semantic Runtime compiler used by
construction tests and compiler-side workflows. It is not the production LOAD
entry point.

Production LOAD uses:

```text
prepare_shm_runtime_v2_persisted()
    -> prepare remaining Type/INLINE-64 area
    -> attach persisted link/init execution columns
```

There is no link/initialization semantic fallback on LOAD. Keeping the semantic
compiler API makes Runtime semantic correctness tests independent of
persistence.


## RUNTIME-V2-DIRECT-EXEC-09B-03A

Persisted Runtime execution now has an explicit direct hot path.

For a persisted link:

```text
source_program == 0 && target_program == 0
    -> mark target directly from link_runtime.target
    -> resolve source directly from link_runtime.source
    -> preserve pending-link recursion/cycle state
    -> no endpoint_program construction
```

`source_reference` still performs the required native reference read and may
resolve a pending dependent link recursively. Complex endpoint programs remain
the fallback only when a persisted program slot is non-zero.

For persisted source initializations:

```text
target_program == 0
    -> memcpy(SHM + target, value, size)
```

Only dereference-bearing initialization targets enter the endpoint interpreter.

This slice does not change `compiled.bin` v23, PUBLISH encoding, BUILD, or the
Type/INLINE-64 preparation boundary.


## RUNTIME-V2-PERSIST-TYPE-INLINE64-09B-03B

PUBLISH now persists the selected INLINE-64 Type execution image itself.

The persisted tail contains only executor state:

```text
type_apis
relative_references
absolute_references
object_references
stores
post_stores
children
repeats
constants
object_where
objects
canonical_roots
object_groups / offsets
canonical_groups / offsets
object_patches
```

Semantic/build caches (`identity_ref` maps, named/derived prepare caches and
other builder state) are not persisted.

The exact Type program size is known only after physical INLINE-64 compilation.
PUBLISH therefore:

```text
encode base compiled.bin
-> prepare SHM ABI layout
-> compile INLINE-64 Type program once
-> extend the SAME final compiled.bin mmap tail to exact size
-> finalize Type section directory entries
-> encode ABI + Type + link/init physical columns
```

No candidate `compiled.bin`, copy or second semantic Graph is introduced.

Production LOAD becomes:

```text
mmap compiled.bin
-> attach ABI spans
-> attach Type execution spans
-> attach link/init execution spans
-> allocate/pretouch SHM
-> execute
```

`prepare_shm_type_batch_inline64()` remains the semantic compiler for
PUBLISH/tests, but is no longer called by production LOAD.

## RUNTIME-SHM-PARALLEL-OBJECTS-09C-02A

The no-pre-touch experiment was rejected by measurement: removing the 24-lane
page establishment pass moved first-touch faults into the mostly serial
object-major executor and approximately doubled LOAD time. Parallel pre-touch
therefore remains part of the production path.

Object construction is now partitioned into disjoint top-level Object ranges:

```text
canonical
-> mark static-link targets
-> parallel Object ranges
-> barrier
-> resolve links
-> source initializations
```

Each lane executes the existing INLINE-64 object-major program. Cross-object
references only write an address into the current Object and do not require the
referenced Object to have already executed. Static-link target markers are
installed before the Object phase and preserved by the normal reference-write
logic.

Lane telemetry is private during execution and reduced after the barrier, so
the hot Object path adds no mutex or atomic telemetry updates.

This first A/B slice deliberately uses a separate worker pool for Object
construction; `objects_ms` therefore includes worker creation overhead.
`runtime_v2_object_lanes` exposes the actual fan-out.

No persisted format, PUBLISH encoding, BUILD semantics or FIXED_DIRECT ABI
contract changes in this slice.

## RUNTIME-SHM-PARALLEL-OBJECTS-NO-PRETOUCH-09C-02B

This A/B keeps the 09C-02A 24-lane Object executor unchanged and removes only
the separate full-SHM pre-touch pass.

The execution order remains:

```text
create zeroed FIXED_DIRECT SHM
-> canonical
-> mark static-link targets
-> parallel Object ranges (first-touch happens here)
-> barrier
-> resolve links
-> source initializations
```

This deliberately does not move or parallelize canonical/link marking. The
experiment therefore measures one question only: whether the already-parallel
Object executor can absorb page establishment more cheaply than the separate
24-lane pre-touch pass.

`shm_pretouch_ms` and `shm_pretouch_lanes` remain in telemetry for direct A/B
comparison and report zero in this slice. `runtime_v2_object_lanes` continues
to report the actual Object fan-out.

No persisted format, PUBLISH encoding, Runtime execution image, BUILD
semantics or FIXED_DIRECT ABI contract changes.

## RUNTIME-SHM-PARALLEL-LINK-MARK-09C-03

Static-link target marking now parallelizes persisted direct targets without
introducing a target hash/map or LOAD-time alias validation.

Graph already enforces one semantic binding per target endpoint. Runtime uses a
stronger physical ownership rule for concurrency: lanes own disjoint ranges of
the SHM target-address space, not link-index ranges.

```text
canonical
-> parallel direct link marking by physical target range
-> barrier
-> serial complex/dereference target marking
-> parallel Object construction
-> barrier
-> resolve links
-> source initializations
```

A direct persisted link has `target_program == 0`, so its final physical target
offset is already present in `link_runtime`. Identical/aliasing physical target
offsets necessarily fall into the same lane and therefore cannot be written
concurrently.

Dereference-bearing targets are data-dependent and remain serial after the
direct-target barrier. This preserves generic correctness without adding any
new persisted metadata or LOAD validation. The production benchmark currently
has zero link dereference targets, so its complete mark phase uses the parallel
direct path.

Each lane owns private mark telemetry; counters are reduced after the barrier.
`runtime_v2_link_mark_lanes` reports the actual direct-mark fan-out.

No compiled.bin format, PUBLISH encoding, BUILD semantics or FIXED_DIRECT ABI
contract changes in this slice.

## RUNTIME-SHM-PRODUCTION-CLEANUP-09C-09

The 09C-04 through 09C-08 diagnostics were temporary experiments and are not
part of the production Runtime API or persisted format.

Measured conclusions:

```text
Object partition:
    equal contiguous live-Object ranges

Object concurrency:
    min(object_count, execution_lane_capacity())

SHM pre-touch:
    disabled

Static-link target marking:
    parallel direct-target marking by physical target range
```

Equal-physical-byte Object partition did not improve LOAD. Explicit Object-lane
caps also did not improve the tested workload; throughput continued improving
through the available execution-lane capacity.

Production therefore keeps no Object lane override, hardware-specific constant,
per-lane profiling vector, work-weight metadata, startup calibration or Server
configuration field.

No compiled.bin format, PUBLISH encoding, FIXED_DIRECT ABI, BUILD semantics or
Runtime lifecycle contract changes in this cleanup.


## RUNTIME-SHM-OBJECT-SCHEDULING-FINAL-09C-13

The Object scheduling experiments are complete.

Measured decisions:

```text
09C-05 equal physical-byte partition      -> reject
09C-08 explicit Object lane caps          -> reject
09C-10 coordinator precomputed ranges     -> reject
09C-11/12 mmap boundary warmup            -> reject
```

The 09C-12 same-binary paired A/B showed no repeatable benefit from touching
persisted `object_runtime` lane boundaries before worker launch.

Production therefore keeps the pushed 09C-03 Object execution path unchanged:

```text
partition      = equal contiguous live-Object count
range math     = worker-local begin/count
concurrency    = min(object_count, execution_lane_capacity())
SHM pre-touch  = disabled
link marking   = parallel direct-target physical ranges
```

No Object lane override, startup calibration, per-lane profiling, work-weight
metadata, precomputed range vector or mmap warmup remains in the Runtime API.

No compiled.bin format, PUBLISH encoding, BUILD semantics, FIXED_DIRECT ABI or
Runtime lifecycle contract changes in this finalization.


## RUNTIME-SHM-OBJECT-TELEMETRY-PRODUCTION-09D-03

The 09D-02 same-binary paired A/B did not show a repeatable Object-phase
performance gain from disabling detailed Object execution telemetry. The
measured difference was within run-to-run scheduler/system noise.

However, the audit exposed a production ownership bug: the parallel Object
helper allocated per-lane execute telemetry and passed non-null telemetry
pointers to workers even when the caller did not request
`project_runtime_telemetry`.

09D-03 fixes that contract without adding a configuration or benchmark switch:

```text
project_runtime_telemetry == nullptr
    -> no per-lane Object telemetry allocation
    -> Object worker telemetry == nullptr

project_runtime_telemetry != nullptr
    -> allocate one telemetry record per active Object lane
    -> collect and reduce detailed Object counters exactly as before
```

The normal Server LOAD path does not request `project_runtime_telemetry`, so it
now performs no discarded Object counter collection. The benchmark still
requests telemetry and therefore keeps all existing detailed counters and phase
timings.

09D-01 trusted validation removal remains rejected and is not part of this
slice.

No compiled.bin format, PUBLISH encoding, SHM layout, Object partition,
concurrency policy or Runtime lifecycle semantics change.

## RUNTIME-SHM-ZERO-STORE-ELIDE-09D-08B

09D-06 measured the Object store population on UnitProXL:

```text
total stores                         27,983,250
zero stores                          27,448,644
non-zero stores                         534,606
```

09D-07 then observed the target bytes immediately before every zero store:

```text
already zero                         27,448,644
clears prior non-zero                         0
```

09D-08A independently reproduced the same execution-equivalent population from
the immutable persisted Type program:

```text
static zero stores                   27,448,644
proven safe                          27,448,644
required                                      0
unknown                                       0

regular proven-safe zero stores      27,338,704
constructor proven-safe zero stores     109,940
```

09D-08B promotes only the architecture-invariant **regular store** case to
production.

The Type Batch builder already rejects record unions. Ordinary scalar
construction stores therefore own scalar member bytes in a fresh-zero
FIXED_DIRECT object. Static-link target markers occupy reference slots, not
ordinary scalar-store slots. If the complete target-ABI object representation
of a regular scalar value is all zero, replaying that store cannot change the
fresh object state.

The builder now applies:

```text
append_store(target, value)

    target-ABI bytes(value) all zero
        -> do not append constant
        -> do not append store_operation

    otherwise
        -> persist exactly as before
```

The check is PUBLISH-time only. No branch, zero test, lookup, side table or
additional metadata is added to the Runtime executor.

This also removes the corresponding unused constant record because elision
occurs before `append_store_to()` creates either physical column entry.

Constructor defaults deliberately remain separate and unchanged:

```text
post_stores
```

A constructor post-store executes after child/base/repeat construction and can
legitimately clear a value written earlier. The measured 109,940 safe
constructor-zero executions are therefore not generalized by this slice.

The persisted path remains:

```text
PUBLISH / REBUILD
    prepare INLINE-64 Type program
        -> regular all-zero scalar stores omitted
    -> compute exact Runtime Type tail counts
    -> extend SAME compiled.bin
    -> encode compact physical columns

LOAD
    mmap compiled.bin
    -> execute persisted program directly
```

09D-08B changes neither Runtime phase order nor native reference/link behavior.
A fresh REBUILD/PUBLISH is required before measuring LOAD because existing
compiled.bin files still contain their previously persisted Type program.

## RUNTIME-SHM-REFERENCE-PROGRAM-CANONICALIZATION-09D-12

09D-10A reduced each relative reference from `{target,source}` to
`{target}+delta`, but the median Object phase regressed. Production therefore
keeps the direct 8-byte pair representation.

The 09D-11 diagnostic then found a different structural property:

```text
reachable relative-reference APIs              5,287
unique exact relative-reference programs         154
duplicate API program copies                    5,133

reachable physical relative-reference records 1,263,229
duplicate physical records                    1,205,064

execution writes in duplicated programs      53,299,841
total Object relative-reference writes       53,471,507
```

The existing Type API already stores a relative-reference program as an
independent immutable range:

```text
type_api.relative_references = { begin, count }
```

Therefore exact program sharing requires no new Runtime representation.

09D-12 runs after final INLINE-64 construction and before persisted Runtime
Type counts are taken:

```text
PUBLISH / REBUILD
    build final INLINE-64 Type APIs
    -> canonicalize exact relative-reference programs
    -> persist physical Runtime Type columns
```

For each non-empty program, the first exact sequence is copied into the
canonical relative-reference column. Later Type APIs with identical
`{target,source}` records reuse the same `{begin,count}`.

The canonicalization hash is only a PUBLISH-time candidate accelerator.
Every candidate match is compared record-for-record. Before replacing the
original vector, a second full audit verifies that every Type API resolves to
exactly the same program it had before canonicalization.

The selection order is deterministic: Type APIs are processed in stable slot
order and the first exact program becomes the canonical physical range.
`unordered_map` iteration order is never used to determine output layout.

Runtime remains unchanged:

```text
for reference in api.relative_references
    write_reference(
        base + reference.target,
        base + reference.source)
```

There is no Runtime hash lookup, additional pointer indirection, arithmetic
reconstruction, branch, LOAD copy or LOAD program rebuild.

`compiled.bin` remains format v24. `type_api` remains 56 bytes.
`relative_reference` remains 8 bytes. Only duplicate immutable records are no
longer persisted multiple times.

This optimization does not change Graph WHO/WHERE identity, Stable-WHERE,
sparse BUILD semantics, FIXED_DIRECT SHM ABI, Object scheduling, link phase
order or Runtime lifecycle behavior.

## RUNTIME-SHM-TYPE-PROGRAM-DUPLICATION-PROFILE-09D-13

09D-12 removed 95.38% of persisted relative-reference records by sharing exact
whole programs through the existing `type_api.{begin,count}` range. It kept the
accepted direct Runtime executor unchanged.

09D-13 is diagnostic-only and asks whether the same architectural optimization
is materially useful for the remaining immutable Type API columns:

```text
absolute_references
object_references
stores
post_stores
children
repeats
```

The profiler uses a deliberately strict equality rule: two programs are equal
only when their complete physical record sequences are byte-identical.

This is a conservative lower bound. In particular:

```text
store programs
    different constant indexes -> different

child/repeat programs
    different type_api indexes -> different
```

even if some deeper semantic equivalence might theoretically exist. No such
equivalence is assumed by this slice.

For each column the profile reports:

```text
physical records
unique exact programs
duplicate API copies
duplicate physical records
potential persisted bytes removable
Object-phase execution operations
Object-phase operations belonging to duplicated programs
maximum exact-program API copy count
```

Storage metrics inspect all persisted Type APIs. Execution-weighted metrics use
only Object roots and propagate exact multiplicity through Child/Repeat edges.
The static Object API application count is printed beside the existing dynamic
Object executor count and must match.

Hashing is only a diagnostic candidate accelerator. Every hash candidate is
verified by exact byte comparison before it is grouped.

The profiler runs only when Runtime prepare telemetry is requested. Normal
Server LOAD with null telemetry performs none of this analysis. The diagnostic
LOAD timing is therefore not a production performance measurement.

`ServerEngineV4PublishBenchmark load` passes a null Runtime telemetry pointer,
matching the normal Server LOAD path. Use it for end-to-end LOAD comparisons.
`ServerEngineV4PublishBenchmark load-profile` requests detailed phase and
program-duplication telemetry. Run each mode in a separate process and compare
production timings only with other `load` runs from the same artifact and build.

09D-13 changes no compiled.bin format, persisted record, Type API range,
PUBLISH output, Runtime executor, SHM ABI, scheduling policy, Stable-WHERE
contract or lifecycle semantics.
