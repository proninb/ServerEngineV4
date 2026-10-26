# PLS10-C1 — Direct Commit Feasibility

2026-10-25. Scope: first feasibility slice on small typed-input fixtures.
Production V2 and A2 are unchanged. PLS10-C integration is **not completed**.

## Three required properties

| Property | Result | Evidence |
| --- | --- | --- |
| Replacement | Yes, for the same encoded endpoint | `graph.cpp` `upsert_initialization()` updates `current.value`, sets replaced and preserves count/slot; real Graph fixture passes |
| Previous origin in SourceMap | Not as exact assignment history | `source_map::add()` deduplicates root/file/data; records do not carry Source offsets or operation order. All contributing roots/files can be preserved, but exact warnings require transient provenance |
| Recover stored Source order from G | No | `object_initialization_record` contains target/value only. Public `initialization()` returns a copy, not its internal index slot. The Graph target index position is private |

Therefore direct physical replacement is supported, but calling Graph in arrival
order is incorrect. A fixture explicitly writes 1, 3, 2 through the raw API and
observes 2. The experimental guard correctly leaves 3 while emitting 1 -> 2 -> 3.

An additional boundary is confirmed: direct-member and one-member path-wrapper
encodings are distinct Graph index keys, even when they denote the same member.
The existing Graph API does not automatically merge these representations.
Direct apply and delayed target resolution must share endpoint normalization.
This is not permission to drop canonical target resolution or union guards.

## Implemented proof

`experiments/parallel-local-source-10-c1/direct_commit.hpp` provides one apply
operation. It compares deterministic Source order before Graph mutation, stores
no direct-value array and invokes existing `add_resolved_initialization()` only
when an operation wins. An unresolved ordinary operation waits for an external
final-target resolver and then invokes exactly the same apply. No second reducer,
Alias Graph, WHO database, persistent history or Graph ABI change was introduced.

For each known cell the proof keeps current event/origin in temporary metadata.
For duplicates it retains only losing event/origin metadata, without values.
Diagnostic projection merges these records by original event, preserving exact
adjacent replacements even when delayed events arrive between earlier direct
events. SourceMap projection preserves losing as well as winning contributors.
Header defaults are not counted as previous Source writes.

## Verification

MSVC Visual Studio 2026 Release + IPO. CTest **1/1 PASS**, comprising seven
fixture groups, including all 24 permutations of four write arrival orders.

- Repeated Graph calls preserve initialization slot/count.
- Delayed order 2 after direct orders 1 and 3 leaves value 3; only two Graph
  mutation calls occur, with one older operation skipped. Warnings are 1->2->3.
- A reference first bound to b and finally rebound to c sends the waiting write
  to c.x, leaving b.x without an explicit initialization.
- All arrival permutations produce max-order value and identical warning chains.
- Cross-root SourceMap retains every contributor; exact file/offset warnings
  survive value replacement. Same-root native contributions are deduplicated.
- Rehash/reallocation across 4097 cells does not invalidate ordering metadata.
- Header member defaults remain unchanged.

Logs: `build/parallel-local-source-10-c1-{configure,build,tests}.log`.

## Measured work elimination and remaining gap

At the fixture kernel boundary, direct values are never copied into a retained
operation array or replayed by a separate Writer. Each winning submission calls
the existing Graph API once; an older delayed submission performs no Graph write.
These are operation-count observations, **not full pipeline performance**.

With strictly existing public APIs, the proof needs an endpoint-to-metadata map
in addition to Graph's own target index. This is explicitly a duplicate lookup;
its cost is not removed by naming the operation apply. No efficiency claim is
made for this container. The prior PLS10-B regression makes that distinction
particularly important.

A compact sidecar indexed by the existing stable initialization slot could avoid
a second endpoint index, but that slot is not exposed by today's public API.
No accessor, producer callback or new Graph API has been added in this slice.
That interface decision and actual integration remain open; they must preserve
comparison-before-replacement, default handling and normalized endpoint keys.

Full Source extraction/linking, A2 reference stabilization, declaration const
construction, SourceMap root assembly and independent runtime oracles have not
yet been wired to this kernel. Consequently there is no UnitProXL A/B here and
no claim of complete PLS10 parity or production speedup. Benchmarking this tiny
standalone kernel would not answer the user's Source-pipeline cost question.

Reproduction: `experiments/parallel-local-source-10-c1/README.md`.
