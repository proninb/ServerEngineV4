# ServerEngineV4 — Graph Identity / Append BUILD handoff

Repository: `proninb/ServerEngineV4`  
Branch: `work/ic-catalog`  
Verified decision baseline: `c5e73eca19065aec8018b875a6ef9ef76e1c73e3`

## Decision

The next Graph architecture is identity-driven:

```text
identity_ref = WHO
Graph handle = current WHERE
```

Do not continue the abandoned persisted tombstone/liveness-bitset direction.

Normal BUILD must eventually become:

```text
changed identity
    -> append replacement semantic payload
    -> patch direct current-location index
    -> old physical payload becomes stale storage
```

There is no semantic version chain (`A v1/A v2/A v3`).

REBUILD is the compaction boundary.

## First implementation slice

`GRAPH-IDENTITY-REF-01A`:

```text
type_ref::named
    current: payload = type_handle slot
    target:  payload = identity_ref(type).slot()
```

Keep current Runtime/layout APIs handle based by resolving:

```text
type_ref named identity slot
    -> graph_identity_index
    -> current type_handle
```

Do not put identity lookup into Runtime cycle execution.

After PASS:

```text
GRAPH-IDENTITY-REF-01B
    base_record::type -> identity_ref

GRAPH-IDENTITY-REF-01C
    object_endpoint::object -> identity_ref
    construction_value::object_binding -> identity_ref
```

Then:

```text
GRAPH-APPEND-01
GRAPH-APPEND-02 sparse persistence
GRAPH-APPEND-03 remove normal-BUILD graph_dense_projection
RUNTIME-CURRENT-01 current-only Runtime construction
```

## Current performance reason

For one changed type, current V4 already has sparse semantic counters, but work after `graph_delta` scales with Project size.

Observed:

| Types | Semantic BUILD | Dense projection | Artifact materialization |
|---:|---:|---:|---:|
| 1K | ~12.94 ms | ~0.049 ms | ~5.85 ms |
| 10K | ~32.45 ms | ~0.469 ms | ~17.58 ms |
| 100K | ~182.20 ms | ~4.94 ms | ~161.94 ms |

Therefore deleting only dense projection is insufficient. Sparse durable persistence is the major target.

## V3 lesson

V3 correctly separated WHO and WHERE, but preserved physical Graph handles with tombstones because persisted relationships still depended on those handles.

V4 already improved one important boundary:

```text
source.bin v7 semantic dependency = exact identity_ref
```

The target completes that design inside persisted semantic G.

## Runtime rule

```text
semantic G
    identity based / relocatable
        -> resolve once during Runtime construction
        -> ABI layout + offsets
        -> FIXED_DIRECT native objects/references
```

No identity/name/hash lookup in execution cycle.

Runtime construction must eventually scale with current semantic entities, never with physical stale payload retained by BUILD.
