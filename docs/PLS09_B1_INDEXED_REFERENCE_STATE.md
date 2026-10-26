# PLS09-B1 — Indexed Reference State

2026-10-25. **Useful experimental candidate; no production integration.**
Only cache/visiting representation changes. Production V2, binding catalog/copy,
diagnostic heads, canonical flat index, parsing, linking order, LWW, Writer,
SourceMap, Runtime, ABI and worker counts remain unchanged. BUILD is out of scope.

## Why the old maps existed

The prototype used `unordered_map<cell_id, cell_id>` for resolved references and
`unordered_set<cell_id>` for active recursion. They made sparse/growing state and
context resets straightforward while proving semantic correctness. They are hash
tables, not ordered `std::map`, and are not required by the semantic contract.
Canonical cell IDs already index a dense local array, so hashing and node
allocation are unnecessary for these two structures.

Other tables are deliberately preserved to isolate this experiment. This does
not claim that binding catalogs/heads fundamentally require node-based maps;
their replacement is separate work. The canonical index still has a composite
WHO/path key and retains the measured PLS03 flat representation.

## Change and lifetime contract

`experiments/parallel-local-source-09-b1/reference_state.hpp` holds 12-byte entries:

```cpp
struct entry {
    uint32_t resolved;
    uint32_t cached_generation;
    uint32_t visiting_generation;
};
```

Each array index is an existing canonical cell ID, not a new WHO/type identity
database. A cached value is valid only in the current binding-context generation.
Visiting has a separate stamp, so a suspended recursive frame is not confused
with a completed resolution. All original paired cache/visiting clear sites,
including caught unresolved operations and binding rounds, advance generation.
At UINT32_MAX the entries are reset before generation 1 is reused.

No pointer or reference into the vector crosses recursion. `enter`, `leave` and
`store` re-index by ID after recursive canonicalization, which may grow cells and
state storage. Failed traversals keep their marks until the original catch/clear
boundary, matching the legacy behavior rather than introducing a new exception
unwind rule.

Generated `alias_linker<false>` keeps the original legacy follow body and
containers; `alias_linker<true>` contains the indexed state and does not construct
unused maps/sets. The native strategy selector is outside alias evaluation, not
a branch per lookup. Missing `PLS09_INDEXED` selects legacy. Same-executable
`PLS08_CANDIDATE=0` still invokes production Source V2.

## Correctness evidence

All ten B1 CTest suites pass:

* 278 existing Source/Writer/native-resident fixture invocations across both
  strategies, including reverse extraction/insertion and negative diagnostics.
* 12 new semantic cases: an initially unresolved indirect target made valid by
  another binding round, a 64-reference chain growing state during recursion,
  and the unchanged depth-limit error. Errors/origins, warnings and numbering
  match across strategies and forward/reverse task order.
* A state suite verifies cell zero, sparse IDs, reallocation across suspended
  frames, stale cache/visiting invalidation, generation rollover and 50,000
  operations compared with the original map/set behavior.

Fresh UnitProXL cold runs preserve:

| Invariant | Both strategies |
|---|---:|
| Semantic fingerprint | 13649286433448276567 |
| Ordered cell ID → WHO/type/path fingerprint | 7605832117458648793 |
| Objects | 137,584 |
| Final values | 705,238 |
| Final bindings | 367,144 |
| Canonical cells | 1,306,064 |
| Linker token reads | 0 |

Native cold checks compare all final values and reference addresses against the
independent verified image. All 2,116,048,327 object/default storage bytes match
the accepted SHA-256
`cb5aa2607c10f928b273ad8f5d33c29f3656190b3ec9597550cbf068a5f0d0e2`.
The complete logical operation/canonical request counts and reuse groups also
match between legacy and indexed allocation-audit runs. Cell numbering is checked
separately from semantic content to detect accidental renumbering.

Every timed run preserves exact same-contract compiled.bin/runtime.bin digests.
V2 has a separate digest domain because its Source semantics/representation
intentionally differ. Existing unsupported Assign/union/scalar-reference-write
boundaries still apply; these tests do not establish arbitrary C++ coverage.

## Allocation audit, outside performance samples

| Metric | Legacy | Indexed | Difference |
|---|---:|---:|---:|
| Reference-stage allocation calls | 734,368 | 101 | −734,267 |
| Whole alias allocation calls | 1,934,385 | 1,200,114 | −734,271 |
| Cumulative alias requested payload | 362.68 MiB | 345.90 MiB | −16.78 MiB |
| Peak tracked alias live payload | 158.35 MiB | 147.29 MiB | −11.06 MiB |
| Tracked retained payload at alias return | 37.38 MiB | 37.38 MiB | 0 |
| Flat canonical-index capacity | 64 MiB | 64 MiB | 0 |

Stage allocations are not all allocations of reference nodes: the original
367,144 cache stores plus 367,144 visiting inserts account for 734,288 node
entries, while the complete old reference stage has another 80 calls for other
growth/output work. The earlier PLS09-A report's exact-two-per-binding arithmetic
has been corrected. New reference-stage calls include dense growth and remaining
work; they are not one allocation per binding.

Allocation wrapping/counters run in a separate diagnostic executable. Its stage
times are not used to claim native PUBLISH acceleration. Requested payload is
not process RSS or OS/allocator overhead. Existing bound buffers predating alias
begin are outside this tracking scope.

## Native PUBLISH A/B

All performance comparisons use one frozen optimized executable and unchanged
native acquisition/Header/persistence/runtime.bin/resident SHM/cleanup paths.
Deep checks, state fingerprints, SHA and allocation counters are outside timed
operations. Full native initialization remains inside PUBLISH; there is no
artificial diagnostic 2 GiB zero vector in timing.

Frozen native executable SHA-256:
`a29a6d0794f629e91311c47a5ec2b190f4a20dcce9f4cfddfd45f18aeeef76d3`.
Inputs and outputs are private build fixtures, not original projects.

### Initial 15 legacy/indexed pairs — inconclusive interval

Native operation medians: legacy 2,128.54 ms, indexed 2,060.83 ms.
Median paired gain +75.13 ms, bootstrap95 −414.47…+97.73 ms, faster 11/15.
Alias paired gain +74.60 ms, CI −106.60…+94.61 ms.

Large within-pair execution-time shifts make this first series insufficient to
establish full-path acceleration. It is retained in full, including losing pairs.
No samples were removed or normalized using other stage times.

### Independent fixed 31-pair replication

Before collecting more data a separate 31-pair run was fixed and announced.
It uses the exact same frozen executable, private input and golden digests,
with alternating pair order, no early stopping and no outlier removal.
Intervals use 20,000 bootstrap resamples of paired gains (legacy minus indexed).

| Metric | Legacy median | Indexed median | Median paired gain | Bootstrap95 | Faster |
|---|---:|---:|---:|---:|---:|
| Native PUBLISH | 2,119.83 ms | 2,044.34 ms | **72.60 ms** | **58.18…88.68 ms** | **30/31** |
| Alias interval | 475.78 ms | 399.80 ms | 74.92 ms | 69.32…83.33 ms | 31/31 |
| Operation + resident teardown | 2,410.75 ms | 2,340.96 ms | 64.09 ms | 25.68…116.07 ms | 24/31 |

Operation includes construction cleanup; operation-plus-teardown adds measured
resident destruction, excluding intervening reporting/cold checks, as in PLS08-C.
Process peak working set remains approximately 2.57 GB, with a paired delta CI
including zero. Lower alias scratch payload has not established a process-peak
RSS reduction dominated by resident SHM.

This replication supports a modest full-PUBLISH gain on UnitProXL. The first
series remains inconclusive; do not pool series silently or subtract a 75 ms
stage gain from historical PLS08 medians measured in another environment.

### Separate 15 indexed/V2 pairs

| Metric | V2 median | Indexed median | Median paired regression | Bootstrap95 |
|---|---:|---:|---:|---:|
| Native PUBLISH | 1,387.65 ms | 2,208.68 ms | **767.52 ms** | **722.40…965.02 ms** |
| Operation + resident teardown | 1,685.26 ms | 2,636.74 ms | 746.55 ms | 688.73…991.79 ms |

Indexed is faster in 0/15 pairs against V2. This is a separate series with its
own conditions; its indexed median must not be mixed with the legacy/indexed
replication median. V2 does not expose an equivalent standalone alias interval,
so its zero alias counter is not a like-for-like stage comparison.

## Decision

Keep indexed reference state as the supported experimental candidate for the
next isolated experiment; keep legacy available as the same-contract control.
It removes the measured cache/visiting node traffic with preserved semantics and
has a positive independent full-path replication. It does **not** justify moving
the whole new Source pipeline into production: V2 remains substantially faster.

Why the large allocation reduction yields only ~73 ms full-path gain: reference
resolution still canonicalizes source paths and forms output, while direct
binding catalogs/copy, duplicate-target heads, extraction, semantic linking,
Writer and native publication remain. Logical counts stayed unchanged. No new
algorithm for those operations was mixed into this patch.

No other maps were replaced, no workers were added, and no subsecond PUBLISH
prediction is made.

Reproduction: `experiments/parallel-local-source-09-b1/README.md`.
Artifacts: `build/parallel-local-source-09-b1/ab-01/` (initial A/B, V2 comparison,
oracles and allocation profiles), `replication-31/` (independent 31-pair series),
plus `build/parallel-local-source-09-b1-tests.log`. All frozen binaries, per-run
stdout/stderr, digests, samples and summaries remain available locally.
