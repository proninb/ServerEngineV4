# PLS07-A — Final Result Ownership Transfer

## Contract

PLS07-A introduces an owning semantic result without compacting its format.
PLS03 flat canonical interning, PLS04 local-file release and PLS06 dense write
heads are unchanged. No workers, Graph materialization, Runtime behavior,
production PUBLISH, persistence or incremental BUILD changes.

Candidate ownership steps:

1. Release the `bound.endpoints` allocation immediately after the last
   write-target canonicalization. Later LWW/diagnostics consume cell IDs,
   write events/origins and paths, not endpoint records.
2. Finish alias reduction and destroy all alias scratch. Paths are still held
   in the original bound arena until its last lookup/hash consumer is gone.
3. Move paths, full write/binding rows, declaration origins and all four result
   vectors into `owned_result`. No offsets or indices are changed, no semantic
   record is copied or filtered, and no additional canonicalization runs.
4. Check exact data-pointer identity for all eight buffers and clear all bound
   storage. Only then construct the read-only consumer view and read/hash/render
   the owning result.

The control uses the same consumer API over the original bound buffers, retaining
the PLS06 lifetime. Both modes share one executable.

## Valid links versus archived endpoint ordinals

Old write/binding transport rows contain indices into `bound.endpoints`.
Keeping those public after freeing the endpoint allocation would export invalid
handles. The new consumer API therefore exports:

- canonical cells and their ranges into owned paths;
- winners referencing owned write records;
- final bindings referencing canonical targets/sources and owned provenance;
- all retained write values, events, physical/value origins and declaration flags;
- all retained binding events/origins and declaration provenance;
- warnings with previous/current origins/events and canonical cell IDs.

The full original rows move into private storage unchanged. Their transport
endpoint ordinals are private archival numbers, not dereferenceable consumer
handles. No values, losing writes or losing bindings are removed. A cycle in
overwritten binding history is not re-evaluated and cannot introduce a new error.

Ownership is self-contained for this semantic output within existing
WHO/type/file-ID domains. Header/type/name catalogs and the file manager are
still external inputs for interpreting IDs/rendering diagnostics; this is not
a complete standalone project database or ready-to-run Runtime image.

## History and incremental BUILD decision

PLS07-A deliberately keeps **all available write/binding rows and declaration
origins**, not only final winners. Compacting to final values would prejudge
future incremental semantics and is not part of this change.

However, this is not a claim of a complete replayable semantic event log. The
old projection already discarded Parser-local declaration/namespace machinery,
and the new public views do not retain typed endpoint syntax for arbitrary
losing assignments. Raw private ordinals alone cannot reconstruct that syntax.
Restoring an earlier writer after Source deletion can require deterministic
Source replay and re-linking, especially when aliases change final targets.

Before implementing incremental persistence, choose and test one explicit
contract: persist enough original symbolic operations to replay binding changes,
or deterministically rebuild them from remaining Source. PLS07-A implements
neither BUILD path and does not use this uncertainty to delete event payload.

## Verification and measurement boundaries

Normal target retains Release+IPO. Allocation audit main uses `/GL-` for the
known MSVC observer LTCG ICE. Both lifetime modes use the same read-only views,
PLS04 memory sampler and existing coarse alias clocks.

`PLS07_VERIFY=1` checks all exported path/index ranges, all retained origins,
history checksum before/after bound teardown, and semantic fingerprint. History
checksum includes every write value/event/origin/declaration flag, binding
event/origins and declaration provenance, including losers. Exact eight-buffer
pointer checks independently prove transfer without record copies. Correctness
checks/oracle are excluded from timed lifecycle; timing runs disable expensive
verification. Consumer fingerprint remains inside lifecycle in both modes.

Endpoint release is separately timed and included in alias/write-target timing.
Ownership transfer/bound teardown is separately timed and included in the full
lifecycle. Owner destruction is included in final IR destruction. Alias phase
clocks freeze before ownership checks/transfer so those costs cannot be
misattributed to alias scratch release. No early-free cost is moved outside the
measured lifecycle.

Full lifecycle starts with Header and lexical input ready and includes
extraction, token/local release, linking, origin validation, aliases/reduction,
ownership transfer, fingerprint consumption and bound/final destruction.
Header/lexical preparation, oracle/ownership verification and profile/report I/O
are excluded. RSS/payload remain distinct: OS process peak covers all prior
process work; sampled working sets are lower bounds and depend on Windows
scheduling; audit requested payload excludes allocator metadata/pre-existing
Header data. This is not a full PUBLISH measurement.

## Tests and artifacts

Eight CTest suites pass, 240 invocations: 184 existing semantic cases across
normal/audit and both ownership modes, 32 sparse/preexisting-cell cases using
owned output, and 24 dedicated ownership/history cases. Cases cover values,
warnings/origins, forward/reverse extraction, empty output, losing writes and an
overwritten reference cycle. Owned consumers read after bound buffers are gone.

Corpus driver performs eight normal/audit × control/owned × forward/reverse
oracle checks with history equality, followed by fresh same-executable timing
pairs and separate frozen-PLS06 calibration. Existing grammar/fail-closed
limitations remain; no full production grammar/BUILD parity is asserted.

Reproduction: `experiments/parallel-local-source-07/README.md`.
Raw ownership reports, memory/coarse profiles, executable hashes, run/pair CSV
and bootstrap summary: `build/parallel-local-source-07/measurements/`.

## Results: ownership, not a speed optimization

All eight corpus oracle runs preserve semantic fingerprint
`13649286433448276567`. The independent checksum of all retained values/events/origins
is `3133748561202576845` in all modes, orders and builds. Writes remain 705,238,
bindings 367,144, linker token reads zero; all retained origins validate after
the move. Every owner reports identical eight-buffer addresses and empty bound
storage before the consumer. No arena clone, record filtering or renumbering.

| Audit invariant | Dependent control | Owning result |
| --- | ---: | ---: |
| Allocation calls | 4,213,254 | 4,213,254 |
| Cumulative requested bytes | 1,614,072,454 | 1,614,072,454 |
| Peak live requested bytes | 374,602,227 | 374,602,227 |
| Tracked requested bytes live after IR release | 20,890,220 | 20,890,220 |
| Retained arena capacity at consumer boundary | 226,862,748 | 176,469,052 |
| Retained capacity, MiB | 216.35 | 168.29 |

The **50,393,696-byte (48.06 MiB)** difference is precisely the removed endpoint
column capacity. Raw write/binding rows, all events/origins they contained,
declaration origins and the full path arena survive under their new owner.
Requests/traffic do not decrease; the change only affects ownership and the
time of endpoint destruction. Overall payload peak is unchanged because it
occurs before the early-free boundary, as anticipated by the PLS05 audit.

Normal-build median transfer/bound-teardown instrumentation is 0.0102 ms in
owned mode (0.0106 ms for control setup); actual endpoint release is 1.5261 ms.
These include coarse clock/setup noise and are not a standalone memcpy speed
comparison. Endpoint release stays inside alias/lifecycle, so cheaper final
destruction cannot be called a full-path gain by itself.

### Fresh paired normal timing

15 alternating AB/BA pairs, all retained, including fluctuations. Positive
saving means lower candidate time/memory. Intervals are 95% percentile bootstrap
intervals for the median paired saving (10,000 resamples, fixed seed).

| Timing, ms | Control median | Owned median | Paired saving | 95% interval | Lower |
| --- | ---: | ---: | ---: | --- | ---: |
| Full experimental semantic lifecycle | 1,487.41 | 1,508.29 | **−2.75** | −24.01…44.50 | 7/15 |
| Alias/reduction including endpoint release | 727.76 | 745.58 | −11.43 | −16.03…33.43 | 7/15 |
| Consumer fingerprint | 126.69 | 126.53 | 0.25 | −1.34…3.18 | 9/15 |
| Final IR destruction | 7.58 | 5.17 | 1.92 | 1.27…2.58 | 14/15 |
| Global semantic linking | 409.18 | 407.50 | −4.38 | −12.83…7.46 | 7/15 |
| External process wall | 7,331.11 | 7,338.33 | 110.08 | −124.99…141.95 | 10/15 |

There is **no demonstrated lifecycle acceleration or regression** in this
series. Independent median differences are not the median paired difference,
and stage medians cannot be summed into an exact total. Final destruction is
cheaper because endpoint destruction moved earlier, not because that work
disappeared.

Absolute times fluctuated substantially during the series. The fresh frozen
PLS06 calibration also ran well above its historical 1.17-second result: old
median 1,778.50 ms versus fresh control 1,802.37 ms. Paired control addition is
60.35 ms, interval −16.81…656.61 ms, too wide to establish a directional wrapper
effect or precise overhead bound. Do not subtract historical timings or infer
an ownership regression from absolute medians. The main evidence is the fresh
same-executable A/B and deterministic allocation/lifetime invariants.

### Resident memory

| Normal-build memory, MiB | Control median | Owned median | Paired saving | 95% interval | Lower |
| --- | ---: | ---: | ---: | --- | ---: |
| OS entire-process peak working set | 595.29 | 595.15 | −0.25 | −1.06…1.01 | 6/15 |
| Sampled lifecycle peak working set | 593.49 | 594.56 | −1.06 | −10.74…1.09 | 4/15 |
| Consumer working set | 458.10 | 413.74 | **44.34** | 43.37…45.06 | 15/15 |
| Sampled peak private committed bytes | 704.87 | 704.90 | 0.24 | −0.05…1.69 | 10/15 |

Consumer residency improves consistently, but **process peak reduction is not
demonstrated**. Retained capacity, working set and peak requested payload are
different quantities and must not be interchanged. Neither a heap purge nor
working-set trim is used.

Compatibility report field `counts.bound_bytes` now records the **input** bound
capacity saved before aliasing/transfer, not storage still owned by the cleared
bound object. Actual retained bytes and transfer checks are recorded separately
in each `.owner.json`; `analysis.json` reconciles those results.

## Decision and PLS07-B boundary

PLS07-A succeeds as an ownership/lifetime contract: final semantic output and
all available value/event/origin payload can be consumed after bound teardown,
without copying arenas or changing checked semantics. Production and the
compact-format experiment remain untouched.

For PLS07-B, distinguish these requirements before changing any row layout:

| Data | Final-state writer / diagnostics | History / incremental question |
| --- | --- | --- |
| Canonical WHO/type/path cells | Required for final targets/sources within the catalog domain | IDs/ranges must stay valid; PLS05 found no unrooted cells on UnitProXL |
| Winner values and final bindings | Required for final state | Selected record indices remain valid in the owner |
| Write/binding events and origins | Required for existing deterministic diagnostics/provenance | Losing rows are retained; future deletion/replay policy is not decided |
| Declaration origins | Required for provenance | Extern/definition history must not silently disappear |
| Original transport endpoint ordinals | Not exposed as valid writer handles | Not a replacement for original symbolic endpoint syntax; Source replay may be needed |
| Header/type/name catalog and object-registration state | Still an explicit external dependency | Not yet materialized from this result into a fresh standalone G |

Only after choosing the writer's actual input API and incremental replay
contract should compact rows or filtered histories be measured. Merely moving
an arena does not remove its construction work or make the whole project
independent of its catalogs. No full event-history deletion, compact ABI,
incremental implementation or Direct G claim is made here.
