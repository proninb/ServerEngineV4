# PLS10-C12 — Existing Batch API Feasibility

2026-10-25. **NO ADOPTION for using the existing batch API in PLS10.**
Source inspection only. No batching implementation, Graph API/ABI change,
benchmark candidate, new index or semantic model was introduced. C10 remains
RETAIN; production Source V2 is unchanged.

## Gate being checked

Can existing `graph::add_resolved_initializations()` replace the string element
loop while exposing the stable slot and prior-source-origin decision *before*
each mutation, without another endpoint lookup or independent target index?

The answer is **no with the current public contract**. This does not prove that
all batching is impossible or that the existing API is unsuitable for V2.

## Existing signatures

`server_engine/project/graph/graph.hpp:523`:

```cpp
server_status add_resolved_initializations(
    type_ref target_type,
    std::span<const object_initialization_record> records,
    bool& any_replaced) noexcept;
```

`object_initialization_record` contains only target and value, not a slot, origin,
callback or per-record output. The only additional result is a single aggregate
boolean. `initialization_apply_result` exists, but is used only by the individual
`apply_resolved_initialization()` API; the batch overload does not accept it.

The individual API (`graph.hpp:513`, implementation `graph.cpp:3639`) accepts a
decision function/context and returns slot/existed/applied. Existing-slot policy
runs before value replacement. New-slot capacity is prepared first; policy runs
before append. PLS10 stores its previous Source origin in a dense vector by this
slot, emits the warning, updates origin and accepts the write in that callback
(`experiments/parallel-local-source-10-c3/commit.hpp:55`).

## Contract comparison

| Property | Existing batch | Individual slot apply |
|---|---|---|
| Record order | Input span order | Call order |
| Temporal reference resolution | Caller must resolve endpoints first | Caller must resolve endpoints first |
| Physical slot stability on replacement | Yes | Yes |
| Return stable slot per record | **No** | Yes |
| Return replacement per record | **No: aggregate OR only** | Yes |
| Callback before each replacement/append | **No** | Yes |
| Preserve previous/current Source origins itself | No | No; producer callback owns them |
| Write SourceMap itself | No | No; producer writes provenance |
| Persistent Graph history / ABI changes needed | None | None |

Temporal references and ordered span traversal are not the blockers. A string
operation can prepare endpoints using the current bindings while no other Source
operation intervenes. The blockers are per-slot observation and the timing of
the producer's origin/diagnostic decision.

## What batch actually does

`graph.cpp:3688`:

1. Validate scalar target type once and prevalidate every target key.
2. Reserve target-index and initialization-vector capacity for the span.
3. For each record, call `upsert_initialization(..., capacity_prepared=true)`.
4. OR its local replacement flag into `any_replaced` and discard that local flag.

`upsert_initialization()` (`graph.cpp:3722`) still performs
`find_initialization_position(target)` per record. An existing record's value
is replaced immediately and its slot is preserved. New records append in order.
No producer callback runs in either case, and the slot is not exported.

Batch therefore amortizes type checking/capacity preparation. It does **not**
remove each element's target-index search or automatically encode endpoints.
No new empirical speedup can be inferred from the source structure alone.

Batch prevalidates all keys before its mutation loop. Individual apply validates
one key, runs its policy, and mutates before moving to the next call. General
failure/diagnostic timing is consequently not interchangeable automatically.
This distinction alone need not block an already-validated string span, but is
another reason not to treat the methods as identical contracts.

## Why post-batch reconstruction fails this gate

For a mixed span of existing and new cells, `any_replaced=true` does not identify
which input records replaced old cells, nor their stable slot indices. Aggregate
new-count information does not recover the old slots. Previous origins are in
PLS10's slot-indexed sidecar, not in the batch records or Graph value records.

For example, a previously written `text[1]` may have a different prior origin
from the rest of the array. A later string must report each actual Source
replacement against its own previous origin. The batch boolean does not map the
new element records to those sidecar entries. Header-seed occupancy also does
not imply a previous Source write: a slot can exist with no Source origin.

Repeated writes to the same target in a general batch lose the per-element
replacement transitions in the aggregate result as well. A normal char-array
expansion has distinct indices, but repeated strings and aliases still require
the general PLS10 slot/origin contract across operations.

Possible workarounds all violate the stated C12 gate:

* Pre/post lookup per endpoint: extra index work and still no batch callback.
* Scan `initialization_entries()` to find old slots: another lookup/pass, even
  if it avoids hashing. Public `initialization(target, record)` itself performs
  a lookup and does not return the slot.
* Maintain endpoint → slot/origin mapping: duplicate index/model.
* Infer slots from append order: valid only with a proven all-new/distinct input
  invariant. UnitProXL's zero existing string slots is observed corpus behavior,
  not the language contract; duplicate-warning fixtures exercise the other case.
* Mutate first, then rebuild warnings: misses the required pre-mutation policy
  point and requires a slot mapping the API has not supplied. Diagnostic failure
  handling also no longer follows individual apply's policy-before-mutation path.

The data might be recoverable with more work; it is not information that the
batch result currently provides. C12 rejects that extra work rather than adding
it and hoping for an unmeasured speedup.

## V2 behavior is not a PLS10 diagnostic oracle

`source_parser_v2.cpp:2082` calls the batch API, then loops over records for
SourceMap, and emits a single `warn_replaced(target)` when `any_replaced` is true.
PLS10 requires per-cell previous/current Source origins and exact warning order.
V2's aggregate warning policy cannot be imported as if it were equivalent.

Also, V2's lower path-intern count is not a property of this batch API.
`source_parser_v2.cpp:2014` independently uses `CW_SOURCE_V2_STRING_PATH_REUSE_01`
and cached element handles, with interning fallback. The batch function receives
already encoded endpoints and contains no call to `intern_resolved_endpoint_path`.
Using it in C10 would not by itself remove C11's 508768 string intern calls.
No new cache is proposed or implemented by this audit.

SourceMap remains caller work (`source/source_map.cpp:855` and the V2 caller's
post-batch loop). Its contribution capture is not a replacement for PLS10's exact
slot-indexed previous event/origin state.

## Decision and validation limits

**C12 = NO ADOPTION of the existing batch API for PLS10 string commit.**
It cannot satisfy the required result/callback contract without another lookup,
additional mapping or a new API. Per the requested gate, implementation stops
here: no batching prototype and no uninstrumented A/B were run. No new runtime
test results or performance measurements are claimed for this source audit.

The 102–103 ms C11 inclusive scope remains an observed area of work, not a
batching savings estimate. A new transient batch-policy API would be a different
explicitly scoped experiment, not a silent extension of C12. There is no reason
from this result to enlarge G, weaken diagnostics or replace the accepted C10
architecture.
