# PLS10-C6 — Ordered Commit Cost Breakdown

2026-10-25. Diagnostic extension only; C5 remains the experimental architecture
baseline and production V2 remains selected. No optimization is adopted here.

## Method and correctness

Frozen optimized C5 executable is compared with a separately frozen diagnostic
executable using generated copies of the same commit and Graph source. Public
Graph layout/ABI, algorithms, Source contract, queue and production files are
unchanged by C6. Four arms are rotated through 15 blocks (60 timed PUBLISH runs):
original C5, observer-off, counters, independent sparse sampling.

Counter totals are exact while observation is enabled. Frequent scopes use a
deterministic hash-selected 1/1024 sample with 15 different seeds. Per-file
wait/take, apply and destruction are timed completely. Selected Graph rehashes,
vector reserves and origin-buffer growth are rare exact timers. Exact rare
descendants are removed from regular samples and accounted separately.

The initial forced-descendant sampler was **rejected**: its extrapolated
exclusive sum substantially exceeded actual apply_file time even though its
overall wall overhead looked modest. Forcing nested clocks perturbed the
sampled calls themselves. Its binary/results are retained under ab-01, not used
for conclusions. Final results use independent scopes (mode 3), contextual
parent/child counts and sampled edge costs. A statistical exclusive difference
is not a directly measured exclusive CPU time.

**288 native fixture runs PASS** across off/counts/independent modes, including
native values/pointers, temporal binding, exact warning origins and identical
compiled/runtime images on the same project paths. A duplicate fixture verifies
initialization lookups 3 calls / 2 hits / 1 miss, two warnings and the expected
link hit/miss counts. Native UnitProXL cold checks in all four arms match the
accepted full 2,116,048,327-byte object-storage SHA. Artifact digests are checked
after every timed run. All 3373 Source files complete.

## Observer price: significant and explicitly retained

Final series `build/parallel-local-source-10-c6/ab-independent-01`:

| Comparison | Paired full-PUBLISH overhead | 95% bootstrap interval |
| --- | ---: | ---: |
| Original C5 -> observer off | +78.14 ms | +34.00..+90.34 ms |
| Off -> counters | +4.06 ms | -7.97..+44.72 ms |
| Counters -> independent sampling | +53.55 ms | +40.28..+123.07 ms |
| Original C5 -> sampling | **+144.15 ms** | **+107.30..+201.54 ms** |

Original C5 PUBLISH median is 2488.28 ms in this fresh series; observer sampling
median is 2629.11 ms. Paired overhead is about **5.8% of full PUBLISH**. Ordered
stage medians are 1141.40 / 1296.49 ms, with paired overhead **154.62 ms**
(122.59..177.18 ms), about **13.5%** of the original ordered stage.

Observation-site/code-layout overhead exists even with counters disabled. These
costs must not be hidden by comparing only sample against counts. Historical
935.439 ms or 2028.98 ms are not controls for this new series; absolute times
changed with the environment. No sampled estimate below is a production timing
or a forecast of milliseconds recoverable by an optimization.

## Coarse breakdown

Medians in the sampled observer, exact timing per file (3373 files):

| Region | ms | Meaning |
| --- | ---: | --- |
| Wait + queue take | **63.77** | Includes lock acquisition, readiness check, condition wait and model take |
| apply_file | **1204.21** | Name/type/endpoint resolution, G work and provenance inside ordered apply |
| Root SourceMap begin/end | **0.46** | 6746 calls |
| Consumed file-model destruction | **3.63** | Main-thread model cleanup only |
| Final committer destruction | **0.76** | Dense provenance and resolver state, outside the historical ordered timer |
| Ordered-stage unmatched residual | **23.99** | Same-run difference; includes queue advancement/notify, setup/finish and observer bookkeeping |

Do not sum separate medians as an exact total. Same-run residual spans
16.62..26.86 ms. Wait readiness is a scheduling observation: median 3320 ready
takes and 53 not-ready takes; it may change when the observer slows the consumer.
These measurements do not prove the unobserved C5 wait is exactly 63.77 ms.
They do show that the observed ordered path is predominantly active apply work,
not destruction or waiting. Worker-local extractor destruction and resident SHM
teardown are different lifetimes and are not included in model_cleanup.

## Exact counts and outcomes

| Operation | Calls | Hits | Misses |
| --- | ---: | ---: | ---: |
| Object begin_endpoint | 946,656 | 946,656 | 0 |
| Initialization target lookup | 705,238 | 0 | 705,238 |
| Initialization origin lookup by slot | 705,238 | 0 | 705,238 |
| Link target lookup, all contexts | 767,104 | 32,816 | 734,288 |
| Link origin by slot | 367,144 | 0 | 367,144 |
| Warning creation | 0 | — | — |

Link lookup contexts are directly counted by observer edges:

- 32,816 lookups during reference traversal, all hits.
- 367,144 lookups in rebind_resolved_link, all misses.
- 367,144 additional lookups in add_resolved_link, all misses.

Thus C5 really performs two target lookups for each new binding: rebind checks
absence and delegates to add, which checks again. This is existing behavior;
C6 does not remove it or claim it explains the whole regression.

Other exact operation counts:

- WHO/symbol binding: **1,169,037**.
- Typed endpoint extraction/resolution from local selectors: **946,656**.
- Subsequent typed resolve in commit: **1,439,526**.
- Typed-path step rewalks: **3,082,404**.
- Target construction sites in resolve/decode: **1,472,342**.
- Endpoint encode calls: **1,472,342** (including existing Graph path interning).
- Reference-follow calls: **32,816**.
- Fine SourceMap/provenance calls: **1,915,204**.
- Declaration events: **137,584**; assignment events: **579,512**.

Construction-site counts are not actual heap allocation counts. There is no
global allocator override; empty target paths may allocate nothing. At least
1,406,710 resolve calls (97.72%) did not require reference following. This is
a structural opportunity, not proof of performance savings.

## Indicative sampled costs, with limits

Regular costs below exclude the separately exact rare-growth regions. They
include observation overhead within the sampled code. Rows overlap where noted.

| Scope | Inclusive estimate median | Range across 15 seeds | Interpretation |
| --- | ---: | ---: | --- |
| typed_endpoint | 337.27 ms | 228.91..410.52 ms | Includes WHO binding, begin_endpoint and member/index transitions |
| WHO binding | 122.91 ms | 80.31..149.82 ms | Nested within other scopes; do not add to typed_endpoint |
| object begin_endpoint | 31.93 ms | 30.12..61.31 ms | Nested within typed_endpoint |
| typed_resolve | 306.60 ms | 228.53..468.31 ms | Includes 32,816 follows; repeated typed-path work and target construction |
| endpoint_encode | 141.78 ms | 113.64..215.57 ms | Includes Graph path interning, not solely resolver arithmetic |
| initialization lookup | 106.16 ms | 81.86..118.61 ms | Miss-only on this corpus |
| all link lookups | 71.77 ms | 51.37..154.45 ms | Contains both new-binding probes and traversal hits |
| physical initialization mutation | 24.02 ms | 20.88..40.18 ms | Appending/replacing record and target-index placement; not entire API |
| initialization origin callback | 22.49 ms | 17.89..68.94 ms | No duplicate warning work on UnitProXL |
| link origin update | 14.11 ms | 10.49..45.79 ms | Separate from link API |
| SourceMap/provenance | 106.35 ms | 69.00..167.96 ms | Includes root-local dedup and no-op BUILD-only capture calls |

Selected rare growth, fully timed: value-index rehash 20.64 ms / 19 calls;
link-index rehash 10.78 ms / 18 calls; value-vector reserve 4.53 ms / 21 calls;
link-vector reserve 2.83 ms / 33 calls; origin-buffer growth 8.79 ms / 68 calls.
The same-run total median is **47.61 ms**. This is not the cost of all allocations:
temporary path vectors inside resolve are not a separate exact allocation audit.

Timer quantization is **100 ns**; empty clock-pair mean is about **17.1 ns**.
Lookup sampled means (initialization miss ~150.5 ns, link hit ~37.5 ns / miss
~95.8 ns) therefore cannot be treated as clean isolated CPU costs. The link-hit
estimate has only about 32 samples per seed. Small API wrapper/exclusive terms
cannot be ranked from these measurements.

Root regular-sample budget plus exact rare work divided by exact apply_file is
0.965 median, range 0.877..1.132. This is a useful consistency check, not a full
accounting identity or calibrated CPU profile. Statistical exclusive medians
for write_request, value_api, link_api, assignment_event and binding_request are
negative. They are retained in the artifacts and explicitly considered
**unresolved**, not clipped to zero or used as an optimization ranking.

## Decision and one next candidate

C6 supports the user's hypothesis that substantial work occurs before physical
Graph writes, in typed endpoint preparation, repeated typed path traversal and
temporary target construction. It does **not** identify an exact production
percentage for each operation or explain all C5/V2 regression causally.

Choose one next A/B candidate: **PLS10-C7 — Already-Typed Endpoint View Fast Path**.
When a path has no dereference and no final-reference follow is required, use
its already validated WHO/root type/value type and a short-lived path span
directly for encoding/commit. Keep the existing temporal traversal for the other
cases. This targets repeated step() validation and owning target-path
materialization, without a cache, new cell index, Graph ABI or queue changes.

This is a single fast-path hypothesis supported by exact counts and broad cost
estimates, not a promise to recover 306 ms. It must preserve path lifetime,
normalization, arrays/inheritance/const, SourceMap and all C5 runtime oracles,
then pass a new uninstrumented paired full-PUBLISH comparison. C7 is **not
implemented** in C6. General forward visibility remains out of this experiment.

Artifacts: `build/parallel-local-source-10-c6/ab-independent-01/summary.json`,
`quality.json`, samples.json, per-run profiles and frozen executable hashes.
The rejected first sampler is under ab-01. Reproduction is in the experiment
README. Python compilation, semantic tests and diff whitespace checks pass.
