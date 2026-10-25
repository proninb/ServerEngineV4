# PLS06 — Dense Write Heads

## Isolated candidate

Only the write-head index changes. Both arms retain PLS03 flat canonical-cell
interning, PLS04 file-local release, binding-head maps, final reference linking,
the existing `next[write]` duplicate chains, LWW, origins and diagnostics.
Production PUBLISH, Graph/ABI, workers and materialization remain unchanged.

`PLS06_DENSE=0` runs unordered-map heads; `=1` runs a `uint32_t` array. Both
arms are in the same Release+IPO executable. Dense growth uses
`resize(size_t(cell) + 1, UINT32_MAX)`, including initialization cost inside
write-target timing. IDs need not be contiguous among writes: cells assigned
during binding and holes are valid, and a first write may arrive late for an
older cell. A source with no writes allocates no dense buffer.

`exchange(cell,write)` returns the previous head; when present it is stored in
the unchanged next-chain array. Dense iteration scans slots in cell-ID order,
skipping the sentinel. Control preserves unordered-map iteration. Per-cell
event sorting, final value sorting and global warning sorting remain in place.
The resulting iteration order can improve locality and value sorting; stage
measurements must distinguish that consequence from faster head insertion.
There is no new per-write instrumentation or timer.

The wrapper retains a default empty map in dense mode, so its small allocation
overhead is included. Head slots/capacity/population statistics are saved only
at traversal boundaries. Temporary array growth and unused capacity are not
excluded from memory or time results.

## Correctness

Six CTest suites pass, **216 fixture invocations**:

- Existing semantic fixtures: normal/allocation-audit × map/dense,
  forward/reverse, 184 invocations.
- Additional sparse/preexisting-ID fixtures: 32 invocations, comparing complete
  values, warnings, origins and fingerprint across both modes and orders.

Special fixtures include 258 cells created before writes, a first write to a
new higher ID followed by writes to an older binding-source cell, hundreds of
holes, mixed direct/alias duplicate chains and an empty write stream. The large
gap case has 259 dense slots with only two populated heads, explicitly checking
that non-write cells do not become phantom writes.

The UnitProXL runner additionally validates normal/audit × map/dense × both
orders against the independent V2-compatible projection, all origins, known
fingerprint, exact counts and unchanged bound/final capacities. Alias-conflict
semantics and warning histories are covered by fixtures, not the old V2 oracle.
The supported grammar/fail-closed limitations remain those of PLS02–PLS05.

## Measurement boundaries

Normal executable retains Release+IPO. Separate allocation-audit main uses
`/GL-` to avoid the MSVC observer LTCG ICE. Requested payload, call counts and
heap live peaks come from the audit; normal timing/RSS do not include those
allocation headers. PLS05 ownership logging is disabled.

The semantic lifecycle starts with Header and lexical input ready. It includes
extraction, token release, linking, local destruction, origin validation,
aliases/reduction, fingerprint consumption and bound/final destruction. It
excludes Header/lexical setup, oracle checks and profile/report I/O. This is not
production PUBLISH, final G or SHM materialization.

Memory observation matches PLS04: sampled working set (requested 2 ms polling,
with Windows scheduling limitations), exact OS entire-process high-water mark
and private committed bytes reported separately. OS peak includes earlier
process work; sampled phase/lifecycle peaks are lower bounds. Requested live
payload is not RSS. Both timing arms pay the same sampler/coarse-clock cost.

Series: eight oracle runs, two warmups, five alternating calibration pairs
against frozen PLS05 normal baseline, then 15 alternating same-executable map/
dense pairs without audit counters or oracle. Confidence intervals use 10,000
bootstrap resamples of median paired savings, fixed seed. Positive savings mean
lower time/memory; independent medians are not the paired statistic.

## Reproduction and artifacts

See `experiments/parallel-local-source-06/README.md`. Raw results, executable
hashes, head metadata, memory/phase profiles, pair order, CSV and bootstrap
summary: `build/parallel-local-source-06/measurements/`. Build artifacts remain
local. No commit or production integration is part of this experiment.

## Main A/B results

All 15 pairs are retained, including slower/noisy runs; no outlier removal.

| Normal timing, ms | Map median | Dense median | Median paired saving | 95% interval | Faster |
| --- | ---: | ---: | ---: | --- | ---: |
| Full experimental semantic lifecycle | 1,356.54 | 1,166.41 | **189.67** | 167.11…204.76 | 13/15 |
| Alias/reduction | 706.71 | 521.80 | **184.92** | 177.12…210.00 | 14/15 |
| Write target canonicalization + heads | 204.20 | 98.10 | 105.99 | 99.05…112.42 | 15/15 |
| LWW + value diagnostics | 34.24 | 8.52 | 25.59 | 23.33…27.28 | 15/15 |
| Value ordering | 28.43 | 3.49 | 24.98 | 24.76…25.46 | 15/15 |
| Write scratch destruction | 36.21 | 0.23 | 36.00 | 33.63…38.23 | 15/15 |
| Global semantic linking | 330.07 | 331.00 | −1.17 | −11.18…6.07 | 7/15 |
| External process wall | 6,134.30 | 5,967.29 | 221.12 | 23.47…265.71 | 12/15 |

The gain exceeds the initial time hypothesis. It is not all faster hash lookup:
there are no 705k map nodes to insert/destroy, dense traversal improves LWW
record locality, and values are produced in cell order before the unchanged
sort. Those effects appear in separate stages. The LWW winner/duplicate
algorithm and all sort calls remain intact. Stage medians/paired savings must
not be summed as an exact decomposition of a total median.

Other alias-stage medians, map → dense: catalog 97.45 → 100.93 ms, reference
resolution 163.79 → 165.91, binding targets 77.64 → 80.86, binding diagnostics
10.60 → 11.77, binding scratch destruction 25.04 → 26.14, final alias-state
release 26.07 → 28.00. These stages were not changed; variation is retained.

## Memory and allocation results

| Normal-build memory, MiB | Map median | Dense median | Paired saving | 95% interval | Lower |
| --- | ---: | ---: | ---: | --- | ---: |
| OS entire-process peak working set | 605.59 | 593.88 | **11.64** | 9.61…13.13 | 15/15 |
| Sampled semantic-lifecycle peak working set | 602.86 | 592.66 | 11.08 | 7.98…26.37 | 15/15 |
| Sampled peak private committed bytes | 717.07 | 703.57 | 14.13 | 10.91…15.93 | 15/15 |

Requested payload comes from separate allocation-audit runs, identical in both
orders for the total/alias high-water marks and count/traffic invariants:

| Allocation audit | Map | Dense | Reduction |
| --- | ---: | ---: | ---: |
| Lifecycle allocation calls | 4,918,502 | 4,213,254 | **705,248** |
| Alias allocation calls | 2,639,633 | 1,934,385 | 705,248 |
| Cumulative lifecycle requested bytes | 1,652,382,385 | 1,614,072,454 | 38,309,931 |
| Alias requested bytes | 418,603,358 | 380,293,427 | 38,309,931 |
| Peak live requested payload, bytes | 391,386,230 | 374,602,227 | 16,784,003 |
| Peak live requested payload, MiB | 373.25 | 357.25 | **16.01** |
| Tracked requested bytes live at lifecycle end | 20,890,220 | 20,890,220 | 0 |

No non-alias phase changes allocation count/traffic. Forward/reverse extraction
can change that phase's transient local-buffer overlap; per-phase extraction
peak is not itself a semantic equivalence invariant. Overall alias payload
high-water marks agree exactly in both orders. The control ledger matches
PLS04/05, providing an additional check on the wrapper's allocation behavior.

640,178 cells already exist when write processing starts. Dense storage ends
with 1,306,064 slots, 705,238 populated and **600,826 holes**. Logical array bytes
are 5,224,256 (4.98 MiB), actual capacity **5,761,608 bytes (5.49 MiB)**. Control
has 705,238 map entries and 1,048,576 buckets. Candidate's empty retained map has
eight default buckets; its small overhead is included in audit counts.

PLS05 measured the matching control map at about 32.14 MiB live payload. The
rough 26.65 MiB steady-storage difference versus dense capacity is not the
whole-process saving: other arrays, index growth and overlapping lifetimes
still contribute to the maximum. Actual global payload saving is 16.01 MiB and
OS resident saving is 11.64 MiB, not the initial 27 MiB storage estimate.

## Semantic and ownership invariants

All eight normal/audit corpus oracle runs pass with fingerprint
`13649286433448276567`. Writes remain 705,238, bindings 367,144, warnings zero
on UnitProXL, linker token reads zero. All 3,373 local file models release
182,960,089 bytes column capacity inside global linking in every mode.
Bound capacity is unchanged at 187,667,640 bytes and final capacity at
39,195,108 bytes. Nonzero warning histories and first diagnostics are covered by
the 216 fixture invocations rather than by this zero-warning corpus.

Dense heads does not change final ownership. Cells still refer to bound paths,
winner records to bound writes, and final bindings to their original provenance
records. It is not valid to free the entire bound model or call this Direct G.

For the next isolated ownership study:

- Final paths can potentially transfer by moving their existing arena after
  alias scratch is destroyed, preserving every range. That removes a lifetime
  dependency without copying, but does not itself reduce arena capacity.
- Write records and declaration origins must survive consumption. All writes
  on this corpus are winners; moving the existing vectors preserves ownership,
  whereas a compact value/event/origin format requires a separate measured
  representation change.
- Binding provenance needs explicit ownership too; the current fingerprint's
  limited use of binding indices does not authorize discarding origins/events.
- Endpoints are not consumed after final canonicalization in this projection.
  Freeing that column is distinct from making all remaining data independently
  owned; peak/lifecycle effects still need measurement.

No such transfer/format/free is included in PLS06.
Performance conclusions currently apply to UnitProXL. Sparse fixtures establish
correctness, not that dense storage is optimal for arbitrarily low write/cell
density. No adaptive index or new policy is introduced.

## Calibration caveat and decision

The initial five frozen-baseline/control pairs coincided with large execution
fluctuations. Median paired lifecycle addition was 114.50 ms, with an extremely
wide interval −312.48…849.20 ms; no useful control-wrapper overhead bound can be
inferred. Those runs remain in the original summary. Main map/dense evidence is
the same-executable 15-pair series above; historical absolute times are not a
substitute. A separate five-pair calibration confirmation is saved without
replacing or filtering either original series.

Confirmation also encountered large fluctuations: paired lifecycle addition
22.48 ms, interval −127.98…440.62 ms; paired alias addition 1.17 ms, interval
−51.20…470.37 ms. Neither calibration establishes a directional wrapper cost
or a precise zero-overhead bound. Both raw calibration series remain available
(`measurements/` and `calibration-confirmation/`). This limitation prevents
using the frozen-baseline comparisons as a precise speedup claim. It does not
replace the pre-specified same-executable map/dense comparison, its retained
outliers or the deterministic allocation/semantic checks. Production adoption
still requires a new controlled full-path measurement.

PLS06 is a successful **experimental** candidate: substantially lower write-head
cost, allocations and peak memory, with preserved checked semantics. Keep the
dense variant available for this pipeline; do not integrate the prototype into
production or claim a production PUBLISH improvement. The experimental lifecycle
median is still about 1.17 seconds before Header/lexical preparation and final G
materialization. The next architectural boundary is final-result ownership,
not another worker pool.
