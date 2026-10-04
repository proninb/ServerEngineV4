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
