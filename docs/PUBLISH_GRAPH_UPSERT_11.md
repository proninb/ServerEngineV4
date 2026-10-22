# Graph insertion algorithm experiments: not adopted

Two variants were implemented and measured against the accepted Source dispatch
version (08), with identical PUBLISH acquisition and durability policy:

1. Link lookup and insertion were combined. The original new-link path probes
   the target index in rebind, again in add, and again during insertion. The
   candidate retained the first vacant slot, probing again only after growth.
2. The same vacant-slot reuse was applied to object initialization insertion,
   in addition to the link change.

Both variants retained canonical append order, stable handles on replacement,
duplicate-add conflict rules, exact target comparison and half-full indexes.
No persisted representation or semantic rule changed.

## Full-PUBLISH results

Eight alternating ABBA/BAAB blocks per variant, two baseline and two candidate
runs per block; a separate warmup block was excluded. Release + IPO, full local
UnitProXL copy, native runs outside the filesystem sandbox. Builds and tests
did not run concurrently with the measurements.

| Variant | Mean saving | Student-t 95% interval |
|---|---:|---:|
| Links only | 3.592 ms | [-9.756, 16.940] ms |
| Links and initializations | 4.047 ms | [-9.022, 17.115] ms |

The combined comparison averaged 1417.956 ms baseline and 1413.909 ms candidate.
Neither variant establishes a repeatable full-operation improvement. Fewer
logical hash probes alone are insufficient evidence of a wall-time gain.
Both algorithm changes were removed; the original Graph implementation was
restored. Raw results are retained under `build/publish-link-upsert-10/` and
`build/publish-graph-upsert-11/`.

All benchmark artifact hashes matched baseline and independent baseline audits
passed. The combined candidate also passed all 22 tests and nine integration
scenarios. A regression test remains for 1024 distinct targets through repeated
index growth, duplicate adds, conflicting adds, stable link rebinding and
initialization replacement. It applies equally to the original algorithm.

## Implication for subsecond PUBLISH

The parallel type-plan stage is bounded by concurrent runtime planning, and
these Graph-probe changes are too small to establish a substantial improvement.
The larger remaining target is Source semantic binding: repeated resolution of
complete endpoints against the immutable Header type domain, string/identity
lookup, and ordered Graph/provenance mutation. Parallel token decoding and raw
Source Model alone have already failed to establish a reliable total gain (07).

A further algorithm change must reduce or parallelize that semantic work while
retaining declaration-before-use, extern visibility, namespaces, inheritance,
arrays, reference transitions, canonical handle order and first-error behavior.
No such larger redesign is implemented by these experiments, and subsecond
full PUBLISH remains unachieved.
