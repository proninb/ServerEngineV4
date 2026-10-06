# PUBLISH performance profile

`ServerEngineV4PublishBenchmark publish` measures the normal PUBLISH path.
`publish-profile` performs the same construction with optional stage timers.
Both modes create and replace the fixture's `compiled.bin` G image and
`runtime.bin` ABI Runtime image; profile timings are diagnostic and should
not be used as the production latency baseline.

Run on a dedicated fixture:

```powershell
build/Release/ServerEngineV4PublishBenchmark.exe publish-profile build/unitproxl-transfer-check/project.json 11814
```

On UnitProXL (11,814 types, 411,415 members, 134,474 objects, 85,731 links),
three warm Release runs measured 1.705/1.721/1.708 seconds. Median phase times:

| Phase | Time |
| --- | ---: |
| Header semantic parsing | 604 ms |
| Physical source acquisition and lexical preparation | 344 ms |
| Source semantic parsing | 299 ms |
| G + ABI Runtime persistence (`compiled.bin` + `runtime.bin`) | 301 ms |
| Resident Runtime/SHM publication | 134 ms |

Header parsing is the largest single stage. Header types have declaration and
base/member dependencies; dividing files among workers without resolving those
dependencies would change semantics. The next optimization experiment should
profile operations inside Header parsing and Graph insertion, then test one
change against the same input and exact output. Lexical preparation already runs
on multiple lanes. Source string assignments currently expand bounded arrays
into per-element initializations; a compact byte-array operation is a separate
semantic/format change and needs its own correctness and performance comparison.

PUBLISH deliberately moves ABI layout and Type execution-plan construction to
the producer so LOAD can attach and execute the physical plan. Moving that work
back to LOAD to improve PUBLISH would violate the current latency tradeoff.

## Include resolution experiment (2026-10-05)

`publish-profile` also reports `include_ms` and `include_count`. This is a nested
part of `header_ms`, not an additional phase. On UnitProXL, 9,853 include requests
spent about 484 ms in resolution, dependency staging and lexical availability.

Successful include resolutions are now cached inside one semantic input's
construction lifetime. The key is the including directory plus literal locator;
quoted and angled forms have separate tables to preserve search precedence.
Failed lookups are not cached. Root preprocessing and pragma-once state still
reset, and every include still stages its dependency and checks lexical state.
The cache is discarded after construction, so another PUBLISH resolves files
again against its own snapshot.

Three alternating warm Release runs, with ordinary PUBLISH timers disabled:

| Variant | Runs (ms) | Median |
| --- | --- | ---: |
| Before include cache | 1710.38 / 1692.05 / 1703.12 | 1703.12 ms |
| With include cache | 1587.16 / 1559.89 / 1625.84 | 1587.16 ms |

Median PUBLISH improved by 115.96 ms (6.8%). Profile medians moved Header from
593.50 to 487.20 ms and include resolution from 483.64 to 379.41 ms.
The before/after compiled artifacts had identical SHA-256:
`BE0097ED35997E45A13992CB4B26B55807A6F2B654636F5F3E0FDD7E6CE99FD1`.
Frontend and compiled-project regression tests passed. A control LOAD completed
in 128.97 ms; LOAD implementation was not changed by this experiment.

## Remaining Header cost

The nested `include_lexical_ms` counter isolates `materialize_and_lex` from
include search and dependency staging. A warm Release run measured:

| Stage | Time |
| --- | ---: |
| PUBLISH | 1595.56 ms |
| Header | 489.45 ms |
| Include total (inside Header) | 379.96 ms |
| Included-file acquisition and lexical preparation (inside include total) | 327.54 ms |

Thus the next large opportunity is physical preparation of included Headers,
not another name lookup cache. Project-declared roots are prepared in parallel,
but newly discovered Headers are acquired and tokenized synchronously during
semantic include replay. Header preparation needs to join the existing physical
worker pipeline without parallelizing semantic type construction.

The design must preserve active include execution order: an included file can
define macros that change later conditional includes. Speculative preparation
must not report errors for inactive includes, register semantic dependencies,
or define Types. It must publish immutable bytes/tokens before semantic use,
retain source-cache invalidation when the byte arena grows, and keep missing
active includes as errors. Adding every file in an include directory to the
semantic root list would change behavior and is not an acceptable shortcut.
The parallel preparation described below implements this phase separation.

## Parallel included-Header preparation

Full construction now inspects lexer directive anchors for direct literal
includes and batches physical read/hash/tokenize work on persistent execution
lanes. It does not execute directives, intern names, assign file identities,
publish dependencies, or build Types on workers. Prepared snapshots and tokens
remain private until authoritative active include replay adopts them in the
original order. Failed speculative acquisition/tokenization falls back to the
ordinary active path, preserving inactive-include behavior and diagnostics.
BUILD baseline replacement remains on its existing path.

Small batches (fewer than eight candidates) use the ordinary path. At most
4,096 outstanding candidates are prepared, with additional includes falling
back to normal acquisition. Each physical path is attempted once per
construction, and each adopted snapshot is removed from the temporary cache.
The source-byte cache is invalidated before adoption can grow File Context.

Three alternating warm ordinary Release PUBLISH runs:

| Variant | Runs (ms) | Median |
| --- | --- | ---: |
| Include resolution cache only | 1619.84 / 1589.18 / 1586.24 | 1589.18 ms |
| Parallel Header preparation | 1471.48 / 1480.26 / 1465.46 | 1471.48 ms |

This change improved median PUBLISH by 117.70 ms (7.4%). Peak working set grew
by about 6 MB. A profile run adopted 1,983 prepared Headers, with Header at
360.18 ms and PUBLISH at 1450.07 ms. The compiled artifact retained the exact
SHA-256 reported above. A control LOAD measured 147.27 ms; no LOAD speedup is
claimed. Regression coverage exercises the actual prepared path, included
macro effects, root replay, absence of inactive file identities, and active
versus inactive lexical/missing-file errors.

## Parallel Project-root acquisition

Initial root preparation now starts the existing persistent execution lanes
before acquisition and reuses them for the lexical pass. Physical read/hash
results are private to workers. The owner prepares borrowed jobs against frozen
paths and publishes results in ascending file_id order, in batches of at most
256 files. Private snapshot bytes are released after publication; a missing or
failed file prevents publication of later results, matching the serial prefix.
Already materialized files and nonlexical roots are skipped. Baseline sparse
replacement retains its existing protocol.

Three alternating warm ordinary Release PUBLISH runs:

| Variant | Runs (ms) | Median |
| --- | --- | ---: |
| Serial root acquisition | 1501.55 / 1510.75 / 1481.46 | 1501.55 ms |
| Parallel root acquisition | 1218.85 / 1243.93 / 1235.52 | 1235.52 ms |

Median improved by 266.03 ms (17.7%). Peak working-set medians differed by less
than 1 MB in this fixture. A subsequent profile run measured initial acquisition
and lexical preparation at 87.58 ms, PUBLISH at 1237.14 ms, and resident creation
at 129.62 ms. The compiled artifact retained the exact SHA-256 above. A control
LOAD completed in 148.30 ms; its implementation was unchanged.

Database regression coverage spans a 270-file batch boundary, mixed Header and
Source roles, a nonlexical root, empty and preloaded files, exact lexical-word
comparison against serial tokenization, and ordered publication on a missing
file. Database, frontend, and compiled-project tests passed; the server was built.

## Compiled persistence stage profile

Optional `publish-profile` counters now split `persistence_ms` into preparation,
initial mapping, Graph encoding, initial view validation, ABI layout, Type plan
construction, tail sizing/remapping/rebinding, physical-column encoding, and
flush. Physical-column encoding has nested ABI, Type, and links/init counters.
These nested values are components, not additional phases. Ordinary PUBLISH
does not read the stage clock. Telemetry is only written by the compiled-artifact
producer lane, with the owner reading it after workers complete.

Three warm Release profile runs measured persistence medians:

| Counter | Median (ms) |
| --- | ---: |
| compiled_prepare_ms | 2.50 |
| compiled_map_ms | 0.34 |
| compiled_encode_ms | 65.00 |
| compiled_validate_ms | 0.0024 |
| compiled_layout_ms | 13.38 |
| compiled_type_ms | 75.60 |
| compiled_remap_ms | 11.18 |
| compiled_physical_ms | 86.53 |
| compiled_abi_encode_ms (nested) | 1.01 |
| compiled_type_encode_ms (nested) | 1.29 |
| compiled_runtime_encode_ms (nested) | 84.31 |
| compiled_flush_ms | 40.60 |
| persistence_ms | 303.02 |

The view binding counter disproves the hypothesis that redundant artifact
validation is a significant cost in this producer path. No validation or flush
was removed, and this change claims no performance improvement. The largest
physical-encoding component includes construction of the links/init execution
plan, not merely copying its columns. A next experiment should split that plan
construction from serialization and assess overlap with the independent Type
plan construction after ABI layout. LOAD must continue attaching persisted plans.

## Parallel Type and links/init plan construction

After ABI layout, PUBLISH now builds the Type program and links/init execution
plan on two execution lanes (one lane executes both tasks on a single-lane host).
Both tasks read immutable compiled/ABI views and own separate output vectors.
The producer joins both tasks before allocating `runtime.bin`, then serializes
the finished plans sequentially there. LOAD maps both persisted images; the
physical Runtime record layouts and executor remain unchanged. The previous combined Runtime preparation/encoding API delegates to
the split preparation and encoding functions for compatibility.

`compiled_plan_ms` is elapsed wall time for both tasks, including worker setup.
`compiled_type_ms` and `compiled_runtime_prepare_ms` are nested individual task
durations and must not be added to plan wall time. `compiled_runtime_encode_ms`
now measures only serialization of the prepared links/init plan.

The refreshed SDK includes model types: 23,333 types, 708,668 members, 137,584
objects, 367,144 links, and a 209,304,064-byte compiled image. Three alternating
warm ordinary Release PUBLISH runs on that workload:

| Variant | Runs (ms) | Median |
| --- | --- | ---: |
| Sequential plans | 1883.23 / 1795.43 / 1875.53 | 1875.53 ms |
| Parallel plans | 1754.35 / 1726.34 / 1753.91 | 1753.91 ms |

Median improved by 121.62 ms (6.5%). One profile run measured Type preparation
at 83.23 ms, links/init preparation at 135.73 ms, combined plan wall time at
135.99 ms, and links/init serialization at 10.33 ms. This profile is illustrative;
the ordinary A/B runs above establish the latency comparison.

Before and after compiled images had identical SHA-256:
`6C9B0EFFFAAA4D615F29B0DB7AD0038BB290AA13E27738E086E35A26542B0358`.
Compiled-project/Runtime, database, and frontend regression tests passed. The
server was built, and a control LOAD completed in 194.40 ms; no LOAD speedup is
claimed.

## Source endpoint resolution

Source diagnostics now report object declaration, value-assignment, link,
endpoint, and direct member-search durations, plus member-search count.
Endpoint and member timers are nested in statement timers. Fine-grained clock
reads add measurable overhead; these diagnostics must not be used as ordinary
PUBLISH latency measurements. One diagnostic run found 1,535,234 member searches,
with objects at 55.30 ms, assignments at 232.04 ms, links at 330.97 ms, endpoints
at 280.08 ms, and member search at 35.93 ms.

A bounded direct member cache was tested and removed. Despite 1,425,211 hits
(92.8%), ordinary PUBLISH medians worsened from 1751.33 to 1785.44 ms in three
alternating runs. The extra key hashing/storage cost outweighed these searches.
No member cache or additional lookup map remains.

Full Source parsing now resolves each endpoint step while consuming its tokens
and commits the already known final type through `intern_resolved_endpoint_path`.
The producer contract requires checked steps over the completed immutable Type
domain. Graph no longer traverses that path a second time to determine its type.
The existing checked API remains for other callers and sparse delta parsing.
String assignment also uses its already checked array element type rather than
resolving the entire prefix again for each character. Inheritance steps append
to the endpoint directly, eliminating the intermediate inheritance vector and
its copy into the endpoint.

The existing canonical Graph path index and final path-column storage remain:
they provide stable endpoint identities, duplicate-binding behavior, and
last-write-wins initialization semantics. This change is one semantic resolution
pass, not a claim that persistence requires no indexing or physical byte copies.

Three alternating ordinary Release PUBLISH runs measured:

| Variant | Runs (ms) | Median |
| --- | --- | ---: |
| Checked path traversal | 1788.81 / 1722.94 / 1780.97 | 1780.97 ms |
| Resolved producer path | 1701.35 / 1742.74 / 1799.16 | 1742.74 ms |

Although the median fell by 38.23 ms (2.1%), two paired runs were slower.
Three additional diagnostic pairs measured Source medians of 663.43 ms before
and 665.49 ms after. These results do not establish a stable latency improvement.
The change removes redundant semantic work without introducing a member cache.

The final compiled image retained the identical SHA-256 reported above and all
137,584 objects and 367,144 links. Frontend, database, and compiled-project tests
passed, and the server was rebuilt. A single control LOAD completed in 228.51 ms;
this is a correctness check, not a LOAD performance comparison.

## Assignment producer commit

Full Source parsing now calls `add_resolved_initialization` and
`add_resolved_link` after checking value/reference compatibility. Graph does not
repeat endpoint type retrieval or compatibility checks in that producer path.
Writable scalar target checking, duplicate initialization replacement, duplicate
link reuse, conflicting link rejection, and provenance insertion remain.
The checked Graph APIs and sparse delta path retain their validation contract.
Bounded char-array assignment uses the same producer commit for each element;
its existing scalar expansion and artifact representation are unchanged.

Optional nested diagnostic counters measure `source_initialization_commit_ms`,
`source_link_commit_ms`, and `source_provenance_ms`. Provenance measures assignment
and link record insertion, not endpoint dependency registration. Ordinary PUBLISH
does not read clocks. Per-operation timing itself increases diagnostic overhead.

After an initial warm-up comparison, three alternating ordinary Release pairs:

| Variant | Runs (ms) | Median |
| --- | --- | ---: |
| Checked assignment commit | 1703.77 / 1726.62 / 1712.86 | 1712.86 ms |
| Resolved producer commit | 1707.98 / 1687.78 / 1736.83 | 1707.98 ms |

The 4.88 ms median difference (0.3%) is within observed variation; a stable
PUBLISH speedup is not established. Two diagnostic runs measured initialization
commit at 100.98/103.06 ms, link commit at 51.42/50.80 ms, and provenance insertion
at 47.55/44.42 ms, including timer overhead. These counters identify work for
further investigation, not its uninstrumented production cost.

Frontend, compiled-project, and database regressions passed. The server was
rebuilt. The final artifact SHA-256 and object/link counts remain identical to
the refreshed fixture above. Control LOAD runs completed in 190.54/193.49 ms.

## Specialized Source statement dispatch

Header roots still complete before Source roots. Source scopes now enter a
dedicated statement dispatcher for object declarations, assignments, links,
namespaces, and empty statements. Header-only declarations preserve their
existing rejection diagnostics. The shared lexer, preprocessing, declaration
helpers, and Graph storage remain; this is a separate Source grammar entry,
not a second lexer or an additional pass over Source files.

When an identifier names a visible object, the dispatcher passes that identity
to assignment/left-endpoint parsing. The endpoint consumes it directly rather
than repeating scope-chain identity lookup. Right-hand link endpoints continue
resolving their own identifiers normally. No lookup cache or map was added.

After one warm-up pair, three alternating ordinary Release pairs measured:

| Variant | Runs (ms) | Median |
| --- | --- | ---: |
| Shared statement dispatch | 1746.60 / 1722.73 / 1745.42 | 1745.42 ms |
| Specialized Source dispatch | 1708.86 / 1727.31 / 1715.03 | 1715.03 ms |

This series shows a 30.39 ms median reduction (1.7%); two pairs improved and
one worsened by 4.58 ms. It is a small measured improvement on this fixture,
not a guarantee for other workloads. Both final artifacts retain the identical
SHA-256 above and all object/link counts. Frontend, database, and compiled-project
regressions passed, the server was rebuilt, and control LOAD took 192.42 ms.

## Flat Source lexical cursor

Source replay now binds one lexical descriptor and, for native construction
storage, one borrowed word span per root. Source has no includes and does not
grow lexical arenas while that root is consumed. It bypasses the Header include
stack and decodes directly into the semantic token, without an intermediate
frontend token or repeated native arena resolution per word. Encoded baseline
words retain their stable descriptor path. Header frames continue using stable
descriptors that tolerate arena growth during include discovery.

A shared checked decoder serves both paths, preserving extended delta/length,
overflow, truncation, and cursor commit behavior. Source identifiers still use
the existing string table; no name cache, new lookup map, token array, or extra
scan was introduced. Preprocessing directives remain rejected in Source.

After one warm-up pair, three alternating ordinary Release pairs measured:

| Variant | Runs (ms) | Median |
| --- | --- | ---: |
| Stack Source input | 1718.24 / 1766.43 / 1713.40 | 1718.24 ms |
| Flat Source cursor | 1716.07 / 1699.72 / 1682.08 | 1699.72 ms |

All three pairs improved; median reduction was 18.52 ms (1.1%) in this series.
Both final artifacts retained the identical SHA-256 above. Control LOAD completed
in 191.93 ms. The server and frontend/database tests were rebuilt; frontend,
database, and compiled-project regressions passed.

The diagnostic run counted 5,888,577 Source tokens and 2,668,839 identifiers.
Nested `source_decode_ms` measured 124.21 ms and `source_intern_ms` 137.75 ms;
both include timer overhead, and millions of per-token clock reads increased
diagnostic PUBLISH to 2240.31 ms. These values are not ordinary production costs.
Normal PUBLISH makes no stage-clock calls.

Additional regression coverage compares flat and stack tokens over extended
source deltas and string lengths, verifies exact positions, Source predefine
isolation, EOF and replay reset, encoded-word extensions, and non-committing
failure for a truncated extension.

## Member spelling experiment and coarse Source measurement

`publish-stage` uses the PUBLISH profile pipeline but disables per-statement,
endpoint, member, token decoding, and string-intern clock reads in Source.
It retains the whole Header/Source phase clocks and other coarse stage counters.
The detail setting survives telemetry reset. Source token counts remain available;
nested Source duration counters are zero in this mode. Use ordinary `publish`
for total latency and `publish-stage` to compare the Source phase with much less
instrumentation interference.

An experiment deferred interning names after dots and compared their text
directly against members in the completed record, including inherited lookup.
No member map/cache was added. After a warm-up pair, five alternating pairs
reversed execution order each round. Coarse Source durations were:

| Variant | Source runs (ms) | Median |
| --- | --- | ---: |
| Intern then member ID comparison | 512.298 / 509.033 / 510.087 / 533.041 / 510.306 | 510.306 ms |
| Direct member spelling comparison | 553.918 / 556.791 / 536.138 / 530.962 / 533.950 | 536.138 ms |

Direct spelling slowed Source by 25.83 ms (5.1%) in this series. Scanning record
members by textual comparison costs more than scanning compact string IDs after
one canonical string lookup on this fixture. The experiment was removed rather
than retained as an optimization. Both compiled artifacts were identical.

The final change instead removes duplicated type-dependency registration:
endpoint selection already records the current type before reading the member
name, so inherited-member lookup registers only the traversed base types.
This preserves registration order and failure provenance, and eliminates a
repeated dependency operation for each member selection. Canonical string IDs
and existing member lookup are unchanged.

Five final alternating ordinary PUBLISH pairs (after one warm-up pair) measured
1700.14 ms median before and 1705.51 ms after. Coarse Source medians measured
516.37 ms before and 524.05 ms after. Neither series establishes an improvement;
the cleanup must not be presented as a speedup. PUBLISH disables BUILD dependency
capture, so the removed registration already bypassed dependency storage/index
insertion and had little work to eliminate. The retained change removes a
redundant call; the new coarse timing mode is the useful diagnostic result.

Final compiled SHA-256 and all object/link counts remained identical. Frontend,
database, and compiled-project regressions passed; the server was rebuilt.
Control LOAD completed in 194.19 ms.

## Runtime physical-plan coarse profile (PUBLISH-RUNTIME-PLAN-PROFILE-01)

On the 23,333-Type UnitProXL baseline, `compiled_plan_ms` is about 131 ms and
`compiled_runtime_prepare_ms` is about 131 ms, while the independent Type plan
takes about 81 ms. Runtime-plan preparation therefore determines the parallel
physical-plan wall time.

This diagnostic slice adds exactly two coarse timers inside the Runtime physical
plan builder:

- `runtime_plan_links_ms` covers the complete Graph link compilation loop;
- `runtime_plan_initializations_ms` covers the complete initialization
  compilation loop.

There are no clock reads inside either record loop. Allocation/setup outside
those loops remains visible as the difference between
`compiled_runtime_prepare_ms` and the two nested counters.

The profile also reports link slots, live links, initialization count, link
dereference count, initialization dereference count, and total dereference
count. The Runtime plan, persisted format, encoder, LOAD path and executor are
unchanged. This slice is diagnostic only and makes no performance claim.

## Parallel Runtime physical-plan producer (PUBLISH-RUNTIME-PLAN-PARALLEL-01)

The coarse profile measured `compiled_runtime_prepare_ms` at 133.12 ms on the
23,333-Type UnitProXL fixture. Of that, link compilation consumed 74.57 ms and
initialization compilation consumed 54.46 ms; about 97% of Runtime-plan time was
therefore split between two independent read-only producer loops.

This experiment runs those two loops on two lanes. It does not partition either
loop by record and introduces no mutex, atomic work index or shared-vector
append. Each lane owns a private `shm_runtime_v2` execution result:

- the link lane produces `links` plus link endpoint dereferences;
- the initialization lane produces `initializations` plus initialization
  endpoint dereferences.

After both lanes complete, one deterministic merge reconstructs the same
sequential physical program order: link dereferences first, initialization
dereferences second. Initialization endpoint `dereference_begin` values are
rebased by the final link-dereference count. Link slots, live-link WHERE order,
initialization order and all encoded record formats are unchanged.

If the two-lane execution infrastructure cannot start or dispatch, preparation
falls back to the previous sequential builder. Semantic errors from either
producer are not retried or masked.

`runtime_plan_parallel_lanes` reports whether the measured preparation used one
or two lanes. The existing coarse link/initialization timers remain enabled for
A/B analysis. `runtime.bin` format, LOAD, Runtime execution and BUILD scope are
unchanged. Performance acceptance is measured separately.

