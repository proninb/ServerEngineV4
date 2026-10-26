# PLS10-C7 — Already-Typed Endpoint View Fast Path

2026-10-25. Result: **retain as an experimental improvement over C5**.
Production V2 remains faster and is not switched. No additional fast-path
complexity or second optimization is included in this experiment.

## Single change

For an eligible already-typed endpoint, commit uses its existing WHO, root type,
value type and a callback-scoped span of its typed path. It skips the owning
target/path materialization and repeated type step() walk. Eligibility requires
no dereference, no required final-reference follow and a validated domain.
All other cases use the original temporal resolver.

A single safety annotation is added to existing event-local endpoint scratch.
It is represented by a private checked_endpoint wrapper; the shared PLS02
endpoint layout and all persistent/Graph layouts are unchanged. The annotation
is obtained during the producer's existing member-resolution loop, not another
path pass. Union and inherited-member paths conservatively fall back so that
existing union/base guards and first-error priority remain intact. String
expansion propagates the annotation to its element endpoints.

No new endpoint index/cache, semantic result model, project-wide array, Graph
mutation algorithm, slot API or Writer is introduced. The C5 bounded worker
queue, project visibility for already declared objects, temporal reference
semantics, origin/SourceMap handling and general-forward-visibility limitation
are preserved. The double lookup for new bindings is deliberately unchanged.

## Lifetime contract

The view is consumed synchronously. Producer path scratch is unchanged during
the callback, and Graph's existing path interner copies/owns the encoded data
before returning. No span survives in G, provenance or the next Source event.
Fallback keeps its owning target alive for the callback. Binding callbacks are
nested, preserving the lifetime of both lhs/rhs, including mixed view/fallback.

## Correctness

**384 native oracle runs PASS**: 32 fixtures x four scheduling configurations x
off/fast/cold-audit strategies. They check native values/defaults/reference
pointers, exact warnings and first diagnostics, persisted compiled/runtime
digests and scheduling equivalence on the same paths.

Focused cases cover repeated buffer growth with 40x128 char storage, nested
union and union-string rejection, unbound-before-union priority, current-binding
reference copy, volatile roots, const member rejection, inheritance fallback and
unsupported virtual-base diagnostics. No unsupported path becomes accepted merely
because its final type was already known.

Cold UnitProXL audit compares each fast view's WHO, root/value types and every
path step against the legacy resolver before consuming it:

| Resolution selection | Calls |
| --- | ---: |
| Verified fast views | **1,403,443** |
| Original resolver fallback | **36,083** |
| Total | **1,439,526** |

Coverage is **97.49%**, slightly below C6's no-follow upper bound because of the
conservative domain guards. These are construction/resolution sites, not a
measurement of heap allocation calls. Empty paths did not necessarily allocate
in C5. The audit executable performs legacy validation and is not timed as a
performance candidate. Native hot counters are compiled out and checked to be
zero in every timed off/fast run. No C6 observer/microtimer is present.

Unmodified UnitProXL completes all **3373 Source files**. C5/off/fast/audit
compiled.bin and runtime.bin are byte-identical at the same fixture path. All
five cold arms, including V2, match the full **2,116,048,327-byte object storage**
SHA including defaults, padding and native references:
`cb5aa2607c10f928b273ad8f5d33c29f3656190b3ec9597550cbf068a5f0d0e2`.
V2's compiled/runtime image bytes remain a separate domain, as before.

## Uninstrumented full-PUBLISH A/B

Release + IPO, original private UnitProXL fixture, frozen executables. Fifteen
rotating four-arm blocks compare original C5, C7 with fast path off, C7 fast,
and V2 selected in the C7 native executable: **60 timed PUBLISH runs**. Comparisons
are matched within each block. Cold correctness/audit runs and post-run SHA
checks are outside this timing series and native PUBLISH timer respectively.

| Arm | PUBLISH median |
| --- | ---: |
| Production V2 | **1357.48 ms** |
| Original C5 | **1732.41 ms** |
| C7 off | **1725.19 ms** |
| C7 fast | **1499.76 ms** |

| Comparison | Median paired gain | 95% bootstrap interval | Candidate wins |
| --- | ---: | ---: | ---: |
| C5 -> C7 off | +1.19 ms | -43.04..+31.67 ms | 8/15 |
| C7 off -> fast | **+222.54 ms** | **+201.16..+236.94 ms** | **15/15** |
| C5 -> fast | **+231.95 ms** | **+212.96..+262.65 ms** | **15/15** |
| V2 -> fast | **-150.56 ms** | **-159.40..-114.70 ms** | **0/15** |

Thus the improvement over C5 is about **13.4% of full PUBLISH**, and is confirmed
both against the original C5 executable and the internal off control. Control
adaptation alone shows no established gain. The single fast-path experiment
passes the user's stopping rule; further complexity is not needed to establish
its benefit.

Ordered stage medians C5/off/fast are 799.526 / 792.222 / **579.030 ms**.
Matched C5->fast stage gain is **220.387 ms**, interval 202.209..237.411 ms,
15/15 wins. This stage includes parsing overlap/wait and model cleanup; it is
not a direct measurement of resolve CPU savings. V2 does not populate this
experiment's ordered timer, so a V2/zero-versus-579 ms comparison is invalid.

Including resident teardown, C5/fast medians are 2035.72 / 1807.28 ms, matched
gain 203.93 ms (181.77..254.94 ms), 15/15 wins. Peak working set versus C5 is
inconclusive; its paired interval includes zero. No allocation or RSS reduction
is claimed from target-site counts alone.

Do not compare these absolute medians with historical C5/C6 series to infer an
extra speedup. This is a fresh control series in the same environment. Nor is
231.95 ms a validation of subtracting C6's sampled 306 ms estimate: that observer
had significant overhead and noisy exclusive terms.

## Decision

Keep the narrow C7 fast path as the next **experimental** baseline for the
supported C5 corpus. It improves full execution materially without a new index
or architecture. Keep temporal traversal and conservative fallbacks unchanged.

Do not switch production: V2 still wins every full-PUBLISH comparison and the
sub-1000 ms goal is not reached. Do not bundle new-slot insertion or double
binding-lookup changes into this result. Any next optimization needs its own
uninstrumented comparison against C7 and V2; the C7 fast path itself should not
be expanded merely to make its coverage number larger.

Artifacts: `build/parallel-local-source-10-c7/ab-01/{summary,samples}.json`,
frozen binaries/hashes, cold reports and `fixtures` oracle outputs. Build/test/run
logs are under `build/parallel-local-source-10-c7-*.log`. Python compilation and
diff whitespace checks pass. Reproduction: the experiment README.
