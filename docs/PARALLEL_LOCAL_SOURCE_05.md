# PLS05 — Bound/Alias Memory & Cost Audit

## Scope and observation

Diagnostic experiment only. Both executables retain PLS04 file-local early
release and PLS03 flat canonical-cell index. No semantic algorithm, scheduling,
materialization, production PUBLISH or consumer format is changed.

The normal executable is the generated PLS04 baseline, Release+IPO. The
separate audit main uses `/GL-` and stateful allocator tags for named bound
columns, scratch containers and result vectors. Allocator rebinding carries
the tag into map/list/bucket allocations. Moves/frees retain allocation
ownership; copies retain destination allocator ownership. The code still uses
the original vector growth, unordered-map operations, flat hash, equality,
first-insertion IDs, reference resolution and LWW.

Every alias-stage allocation high-water mark captures the simultaneous live
requested payload by owner. This includes temporary coexistence of old and new
vector/hash buffers during growth, unlike an end-of-loop capacity sum. `other`
covers tracked lifecycle allocations not assigned a named owner, including
construction G and string/identity state. Header/file allocations made before
the measured lifecycle are excluded from this payload ledger, but are present
in process working set.

Checkpoints record owner payload, bound array size/capacity, result counts and
current working set at stage transitions. Observer checkpoint logs and
reachability-analysis bitmaps are excluded from allocation counting. They do
affect audit timing/RSS. Audit results cannot be presented as normal performance
or an exclusive CPU decomposition. Fresh normal-build coarse clocks supply
the cost baseline; audit-versus-normal pairs quantify observer perturbation.

Output reachability uses role masks: value cell=1, binding target=2, binding
source=4, warning cell=8. It counts the union, not the sum of overlapping roles.
Path reachability counts physical path-array positions, not distinct semantic
path contents. Unrooted-at-output does not mean dead earlier in the algorithm.

## Last consumers and lifetime constraints

| Structure | Required through | Why / constraint |
| --- | --- | --- |
| Bound endpoints | Last write-target canonicalization | Supplies WHO/root/final type, original typed path and origin; later LWW uses cell IDs rather than endpoint syntax. Current fingerprint does not read endpoint records. |
| Bound paths | Final consumer | Canonical cells retain ranges into this arena. Reclaiming unused slots requires ownership transfer/compaction and range remapping. |
| Bound writes | LWW and final consumer | Winner stores a write index; value, event and origins stay in the write record. |
| Bound bindings | Reference resolution and binding diagnostics; selected provenance afterward | Current final binding retains the original binding index. Its origins/events cannot be discarded without an explicit output ownership contract, even if the fingerprint hashes only canonical target/source. |
| Declaration provenance | Final consumer | Fingerprint/consumer reads all declaration origins, including extern declarations. |
| Flat canonical index | Last canonicalization | Later LWW and output ordering use stable cell IDs. Its equality/hash still require paths while lookup is active. |
| Canonical cells | Following references, LWW diagnostics, final consumer roots | Follow and output refer to cell IDs. Arbitrary removal/renumbering breaks those references. |
| Reference cache / binding lookup / Header lookup | Potential last follow during write canonicalization | Generic defaults/nested cases must be considered; immediate UnitProXL references alone do not prove early release safe for all fixtures. |
| Cycle set | Active reference recursion | Must be empty outside recursion, but bucket capacity remains until destruction. |
| Direct/convergence map, indirect list | End of binding-linking scope | Current lexical scope retains these past catalog construction through binding diagnostics. |
| Binding heads, next/history | Binding diagnostics | Build duplicate chains and deterministic origin/event warnings. Destroyed on return from binding linking. |
| Write heads, next/history | LWW/value diagnostics | Required after target canonicalization until winners and warnings are formed. Destroyed on return from write reduction. |
| Final values/bindings/warnings | Final consumer | Small records still refer to canonical cells and bound write/binding records; result is not a self-contained materialized G. |

Early release of the endpoint column after write-target canonicalization is a
candidate for the **current projection**, not an implemented change or a
production-consumer guarantee. Dropping paths or selected provenance then is
not equivalent. The audit does not mutate columns to manufacture a memory win.

## Correctness and reproduction

Normal and audit CTest suites pass, 92 fixture invocations in total, including
forward/reverse, aliases, nested targets/defaults, cycles, repeated writes and
bindings, exact warnings/negative diagnostics, enum/string/array values,
origins and Project order independent of file IDs. The allocation-tag wrapper
was corrected before corpus measurements to avoid initializer-list ambiguity
for numeric vectors; no failed preliminary run is timing evidence.

Corpus runner checks normal and audit forward/reverse against the independent
V2-compatible projection, all origins, zero linker token reads, PLS04 early
release and ledger call/byte reconciliation. Changed alias-conflict behavior is
covered by fixtures, not the old V2 oracle. PLS02/03 grammar limitations remain.

See `experiments/parallel-local-source-05/README.md`. Raw reports, ownership
snapshots, memory logs, executable hashes, paired runs and bootstrap summary:
`build/parallel-local-source-05/measurements/`. Artifacts remain local.

## Simultaneous peak composition

Forward and reverse audit ledgers agree exactly. The audit reconciles with the
PLS04 lifecycle allocator: 4,918,502 allocation calls, 1,652,382,385 cumulative
requested bytes. Alias-stage peak live requested payload is **391,386,230 bytes
(373.25 MiB)**, the same as PLS04. This is a measured allocation high-water
mark, not the sum of independently observed structure peaks.

| Category at the same alias peak | Bytes | MiB |
| --- | ---: | ---: |
| Retained bound columns | 187,667,835 | 178.97 |
| Alias scratch | 138,033,447 | 131.64 |
| Output vectors, including growth overlap | 44,794,568 | 42.72 |
| Other tracked lifecycle data | 20,890,380 | 19.92 |
| Total | 391,386,230 | 373.25 |

The peak occurs during growth of `out.values` in LWW: old and new value buffers
coexist. Bound columns, completed canonical cells, flat index, reference maps
and write heads are all still alive. It is **not** the flat table's individual
rehash peak. Individual flat-index allocation peak is 96 MiB, but only 64 MiB
of it is present at the overall alias high-water mark. Summing the former with
all other independent maxima would invent a peak that never happened.

| Owner at the same alias peak | MiB | Allocation calls over lifecycle |
| --- | ---: | ---: |
| Bound typed paths | 51.77 | 35 |
| Bound endpoints | 48.06 | 36 |
| Bound writes | 56.07 | 35 |
| Bound bindings | 17.80 | 33 |
| Bound declaration provenance | 5.27 | 30 |
| Flat canonical-cell index | 64.00 | 20 |
| Reference cache | 16.40 | 367,158 |
| Binding lookup | 16.40 | 367,147 |
| Write heads | 32.14 | 705,253 |
| Write next-chain array | 2.69 | 1 |
| Canonical cell output | 24.03 | 36 |
| Value output, old + new buffers | 13.35 | 35 |
| Binding output | 5.34 | 33 |

Header lookup/cycle-set buckets and write-history temporary contribute a few
hundred bytes at this peak. Warnings are zero on UnitProXL. Warning storage and
origins are validated by fixtures rather than inferred from this zero count.
MSVC large-vector allocation alignment can add 39 requested bytes per buffer;
operator-new requested bytes therefore differ slightly from element capacity.
Allocation-header/malloc overhead and pre-existing Header/input data are not
included. RSS cannot be attributed to each owner by adding these payloads.

Direct/convergence and binding-head maps are already dead at the overall peak.
Each nevertheless made 367,158 allocations; cycle tracking made 367,146 and
canonical traversal temporaries made 98,448. These are earlier costs, not
simultaneously retained memory at the maximum. Full owner tables, including
cumulative requested traffic and individual peaks, are in `peak-ownership.csv`.

## Lifetime checkpoints

| Checkpoint | Live tracked payload, MiB |
| --- | ---: |
| Alias start (bound + retained construction state) | 198.90 |
| Binding catalog built | 254.82 |
| Reference resolution complete | 297.91 |
| Binding targets/diagnostics, scratch still in scope | 315.71 |
| Binding local scratch released | 279.72 |
| All write targets canonicalized | 359.91 |
| Actual growth high-water mark in LWW | **373.25** |
| LWW complete, write scratch still in scope | 367.92 |
| Write local scratch released | 333.08 |
| Alias object/scratch destroyed | 236.28 |

Boundary samples alone miss the growth spike. `checkpoints.csv` includes all
owner contributions and audit working set at each boundary. Audit working set
is diagnostic only because logging, analysis buffers and allocation headers
perturb it. Normal baseline RSS remains the appropriate process-memory control.

Freeing endpoints/index/maps only **after** write-target canonicalization could
reduce later LWW residency, but leaves the already observed 359.91 MiB live
payload unchanged. With all earlier work unchanged, the maximum possible
whole-alias payload reduction from such late frees alone is at most 13.35 MiB
(373.25 − 359.91), possibly less because of earlier transient peaks. This is
not a prediction of RSS savings. It cannot justify a 50–120 MiB process-peak
claim on its own.

## Canonical-cell hypothesis: no unrooted cells on UnitProXL

| Disjoint final role | Cells |
| --- | ---: |
| Value only | 665,886 |
| Final binding target only | 367,144 |
| Final binding source only | 233,682 |
| Both value and final binding source | 39,352 |
| Unrooted from current output | **0** |
| Union | **1,306,064** |

705,238 value cells are the first and fourth rows together. The extra 600,826
cells are binding targets and source-only cells, not abandoned intermediate
endpoints. All canonical cells survive as direct output roots under the current
representation. There is no corpus evidence for simply deleting half the cell
catalog. A different compact representation might help, but requires a new
output contract and cannot be justified by the value/cell count ratio.

All 705,238 write records are selected by values, and all 367,144 binding
records are selected by final bindings on this corpus. Fixtures do have losing
writes/bindings; they require their warning/provenance semantics to be preserved.

Of 3,212,650 physical bound path slots, final canonical cells reference
2,861,120; 351,530 slots are not used by final cells. All cell-referenced slots
are output-referenced because no cells are unrooted. Unused slots plus path
capacity slack amount to about **8.11 MiB**. This is only an element-storage
upper estimate for an eventual compact final path arena; it requires range
remapping and says nothing about safety before canonicalization completes.

## Bound and final representation

| Bound column | Logical bytes | Capacity bytes |
| --- | ---: | ---: |
| Paths | 51,402,400 | 54,286,720 |
| Endpoints | 46,573,600 | 50,393,696 |
| Writes | 39,493,328 | 58,792,664 |
| Bindings | 14,685,760 | 18,664,360 |
| Declaration provenance | 5,503,360 | 5,530,200 |
| Total | 157,658,448 | 187,667,640 |

About **28.62 MiB** is capacity slack, mostly writes. It is not duplicate
semantic content. A `shrink_to_fit` copy would temporarily hold both buffers
and costs time, so this audit does not insert one or count this as a win.

Final output retains 39,195,108 bytes capacity (37.38 MiB); its logical cell,
value and binding elements use 30,944,656 bytes (29.51 MiB), with no warnings on
this corpus. It remains dependent on bound paths, write values/origins and
selected binding provenance. Current fingerprint does not read bound endpoints
or the final binding's original-record index; this does not authorize removing
provenance required by a future writer. Bound/final ownership must be made
explicit before claiming a self-contained result or Direct G.

## Cost baseline and observer control

Fresh normal-build medians from five control runs paired with the audit:

| Coarse phase | ms |
| --- | ---: |
| Binding catalog | 85.71 |
| Reference resolution + binding output | 150.37 |
| Binding target canonicalization + heads | 76.25 |
| Binding diagnostics | 9.07 |
| Binding scratch release | 24.77 |
| Write target canonicalization + heads | 201.33 |
| LWW + value diagnostics | 34.96 |
| Value ordering | 28.01 |
| Write scratch release | 35.40 |
| Alias-state release | 26.27 |

Normal alias median is 690.78 ms and experimental lifecycle median 1,290.23 ms.
Stage medians are not additive and these are not PUBLISH times. Counts/owned
bytes identify allocation pressure, not exclusive CPU cost per container.

Audit adds a median paired **250.06 ms** to lifecycle (95% bootstrap interval
224.41…285.77 ms) and 230.03 ms to alias timing. Its process peak also grows by
about 47.62 MiB due to observation/allocator effects. Thus instrumented clocks
and RSS are explicitly unsuitable for optimization ranking or performance
claims. Five baseline calibration pairs (frozen PLS04 versus fresh normal
PLS05) yield paired lifecycle addition 44.93 ms, interval −18.53…92.33 ms:
directional change is not established. Historical absolute timings are not an
A/B optimization result. Intervals use median paired differences and 10,000
bootstrap resamples with fixed seed.

## Decision

PLS05 closes as a read-only ownership/cost audit. Correctness is preserved:
normal/audit forward/reverse oracle runs have fingerprint
`13649286433448276567`, zero linker token reads, valid retained origins and
identical allocation ledgers/role counts in both orders. No production change,
new worker pool or materialization is introduced.

The next narrow candidate is **dense write-head storage keyed by existing cell
ID**, replacing only the `write_heads` unordered map. IDs are already dense and
stable. The current map uses 32.14 MiB at the peak and 705,253 allocation calls;
one 32-bit head per final canonical ID would have 4.98 MiB logical storage.
Dynamic growth, initialization, ordering, warning equivalence and full lifecycle
still need an A/B experiment. The approximate 27 MiB storage difference is not
a measured RSS/time saving. Do not promise 50–150 ms or 50–120 MiB in advance.

This candidate attacks scratch present throughout the expensive write-target
stage, rather than freeing a large column only after the high-pressure work is
already done. Keep flat interning, reference linking, bound format, early local
release and provenance unchanged in that experiment. A full bound-model rewrite
or parallel extraction is not justified by this audit alone.
