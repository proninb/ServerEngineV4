# PLS07-B1 — Final Writer Contract, revision 1

Status: experimental contract and executable reader checks. No compact format,
Graph writer implementation, new workers, production change or speed claim.

## Decision

Separate **Final State** from **Replay Input** logically. They need not have
separate physical buffers. A final writer consumes already resolved state and
matching catalogs; it does not consume Parser tokens or operation history.

For the first implementation of the new BUILD semantics, choose
`reparse_all_remaining_sources_v1`. On relevant input changes, recompute Source
semantics from coherent remaining Source snapshots in declaration-order DFS.
Do not derive replay from winners, SourceMap membership, warnings or private
archived endpoint ordinals. Do not silently reuse production sparse invalidation
as an algorithm for the new alias-aware semantics.

PLS07-A retains its current history payload. This decision authorizes neither
filtering losing rows today nor a compact representation. Those are subsequent
measured changes after the writer/replay interfaces are established.

## 1. Final State API

Executable declarations: `experiments/parallel-local-source-07-b1/`:

- `final_writer_contract.hpp`: typed logical views, `FinalStateReader`,
  `CatalogReader`, replay request and writer operation declaration.
- `owned_reader.hpp`: borrowing adapter using only PLS07-A public `state()` and
  `view()` APIs. No arena copy, history scan, endpoint syntax or bound_model.
- `write_final_state(reader, catalog, candidate_seed, report)` is declared but
  deliberately **not implemented**. `candidate_seed` is an opaque future token,
  not a new Graph layer or persisted ABI.

The physical representation can change behind this API. Small returned views
are not a compact storage proposal, on-disk struct layout or promise of exact
record sizes. Spans/views borrow immutable owners that outlive the writer.

| Logical input | Required fields / role |
| --- | --- |
| Snapshot domain | Nonzero lineage + snapshot tokens, semantic contract revision 1; same ID domains across state, catalogs, Source plan and seed |
| Canonical cell | Snapshot-local `cell_id`, object WHO, root object type, resolved value type, typed member/base/array path |
| Winning value | Target cell ID, normalized construction value, declaration-initializer flag, event and provenance |
| Final Source binding | Target/source canonical cell IDs, winning event and provenance |
| Declaration occurrence | WHO, extern/definition role, event, producer root, name and statement origins |
| Unique object catalog | WHO, resolved type, semantic flags, classified cold-construction value/origin; unique registration facts, not declaration occurrences |
| Diagnostics | Precomputed warning transitions with canonical target, previous/current events and physical origins, reference/value category |
| Source plan | `(file_id, source_order)` from declaration-order DFS; root lookup for event ranks; file ID is not priority |
| Type/identity/string/file catalogs | Matching immutable domains, type definitions/defaults/ABI inputs and identity interpretation; existing lookup infrastructure |

`cell_id{0}` is valid. It is not an `identity_ref`, Graph path handle, object
WHERE, runtime offset or persistent cross-generation address. Its links/ranges
are valid within the owning result and its snapshot. Writer-generated Graph
handles belong to the output candidate and never replace logical event order.

The catalog is an explicit additional input. PLS07-A does not own object types,
registration flags, Header construction programs or the identity/string/file
tables. They currently remain in the semantic G/context. This gap must not be
hidden by calling the current owner a complete standalone project database.

### Producer root versus physical origin

Provenance carries both the logical producer root and physical source locations.
`event.source_order` maps through the frozen Source plan to the producer root.
The physical operand/target location can belong to another file, e.g. expanded
input. Neither its numeric file ID nor its physical position decides the winner.
The adapter obtains roots from the supplied plan without scanning history.
That plan is part of the same snapshot domain, not a mutable external schedule.

### Objects and seed

The first backend may operate on the current **seed G**: Header/type/default
facts plus complete validated object registration, before the Source final
overlay is applied. It must not register the same objects a second time.
The catalog covers every object needed by the output, including baseline objects;
declaration occurrences alone do not provide a unique object/type table.

Seed must contain no stale previous-generation Source initialization/link
overlay. Source replay starts from Header facts, not a completed previous Source
G with old winners still installed. Existing identity lineage may be reused to
preserve WHO, without retaining old Source semantic facts.

`candidate_seed` must eventually attest matching domains, required baseline
facts, object registration and candidate ownership. No token factory/backend
is implemented in B1. Caller-supplied domain numbers are coherence markers, not
cryptographic proof; the future trusted producer must bind them to real snapshots.

## 2. Source overlay, defaults and Runtime

Reference coverage is explicitly
`source_overlay_on_catalog_defaults`. PLS07-A final bindings enumerate final
**explicit Source** bindings, not every effective Header/member/default binding.
Header defaults and baseline links remain part of the catalog/seed. Omitting
them because they are absent from the Source overlay would change semantics.

The writer must obey these rules:

1. Input is semantically closed for the supported grammar. No unresolved WHO,
   type, alias or pending operation reaches writer. Canonical cell paths have
   no dereference steps; a binding target can still be reference-typed storage.
2. Winners are already selected against final bindings by `(source_order,
   local_order)`. At most one explicit value and one explicit binding per
   respective canonical target. Writer never repeats LWW, name lookup or syntax.
3. Preserve Header/type/object default semantics as the baseline. Explicit
   Source values and bindings override that baseline under the accepted
   final-cell contract. Header defaults are not duplicate Source writes.
4. A declaration initializer is an explicit Source occurrence, not an implicit
   Header default. It is present in final-value reduction. A cold object
   construction value/flag may also carry it in today's semantic G. The backend
   must reconcile this with final values rather than blindly treating both as
   independent winners. Final values are authoritative for final-state query.
5. Preserve final reference topology before final value application. Runtime
   currently materializes objects/type programs, then links, then explicit
   initializations; this is a useful compatibility point, not a proof that any
   arbitrary seed/encoding produces correct new semantics.
6. Compiled G, effective query, LOAD, Runtime and SHM must agree on the same
   final state. A runtime-only winner list with stale compiled/query state is
   invalid. Source declaration cold metadata must not be reported as a current
   value when a final explicit override exists.
7. Before parallel memory stores, prove physical storage ranges are disjoint.
   Different symbolic cells or starting offsets alone do not prove this for
   overlapping layouts. Unsupported layout/default/reference cases fail closed.

Current readiness is **`projection_only`** by default in the adapter. A future
completed producer may assert `closed_supported_semantics` only after coverage
and materialization gates are satisfied. The cold checker can inspect a
projection for diagnostics but does not promote it. Its `ok` means envelope/
reference checks passed, not that layout, source syntax or Runtime are proven.

## 3. Writer duties and exclusions

Logical operation:

```cpp
writer_status write_final_state(
    const FinalStateReader& final_state,
    const CatalogReader& catalogs,
    candidate_seed& unpublished_candidate,
    writer_report& report);
```

This is conceptual syntax; the executable C++20 declaration is templated over
the reader concepts. It has no Replay Input/history parameter.

Writer duties:

- Check domain/readiness/seed compatibility once and use existing WHO/type
  catalogs; do not create a second WHO/type database.
- Encode referenced canonical cells into Graph endpoints once, using resolved
  root/value types and the existing resolved path API. Empty paths mean whole
  object storage and must be encoded explicitly, not mistaken for member zero.
- Store final Source bindings/values and their required provenance; preserve
  semantic flags/declaration facts. No intermediate Source execution timeline.
- Produce deterministic physical records/indices for the new contract. Physical
  ordering need not be old V2 ordering and cannot choose semantic winners.
- Build SourceMap using the **new** Graph handles. Group provenance by producer
  root as required by root-contiguous storage, independently of Graph append
  order and independently of winner priority.
- Keep diagnostics in the already defined event/tie order. Graph's `replaced`
  flag is a physical storage result, not a reason to invent/drop Source warnings
  (e.g. overwriting a Header-default link is not a duplicate Source statement).
- Leave a candidate that owns all materialized data and does not borrow the
  semantic owner's arenas after writer completion.

Writer does not parse/retokenize Source, resolve aliases, infer losing history,
renumber global WHO, select diagnostic priority by completion time, or perform
incremental invalidation. Artifact persistence and publication are subsequent
transaction steps, not implied by successful in-memory writing.

On unsupported semantics/encoding or failure, the candidate is not published.
Do not silently fall back to old V2 winners or emit a partially updated artifact.

## 4. Replay policy v1

Replay Input is an immutable Source acquisition/context request, not a second
copy of final state and not a log derived from `owned_result`.

| Required replay input | Meaning |
| --- | --- |
| Current Project/Source plan | Remaining roots in declaration-order DFS with stable physical file IDs and new temporary ranks |
| Source snapshots/provider | Coherent bytes/effective input acquired by existing Source Manager for this transaction |
| Header/type/preprocessing context | Include closure, predefines, visibility/type context and matching catalog generation |
| Language/semantic revision | Includes alias-aware final-cell policy and supported grammar revision |
| Destination lineage/snapshot | Bounds all output IDs and provenance to the candidate generation |

The executable `replay_request` contains opaque nonzero revision tokens and
borrowed task descriptors. Snapshot content/provider and Header builder remain
explicit external services. These tokens introduce no SHA-256, USN or extra
post-read freshness checks. They are not an on-disk format or a new Source
Manager implementation.

On Source edit/removal, binding change or Project reorder, the initial new BUILD
path re-extracts **all remaining Source roots** in the current plan, links/types
them in the established semantic order and computes new final bindings/cells/
winners/diagnostics. Header/preprocessor/configuration changes first rebuild
the corresponding context, then replay all remaining Source. Zero remaining
roots is valid and yields the Header baseline without old Source overlay.

A coherent Source removal updates the manifest/plan; a missing file still
referenced by the manifest is an acquisition error, not an implicit successful
deletion. Replay must not silently skip it.

This is one syntax pass per Source in the **new generation**, not a second syntax
pass by final writer. Global linking still consumes extracted semantic records.
No old canonical target or old final binding is reused merely because a writer's
own text did not change.

Consequences:

- Removing the winning Source restores an earlier remaining writer by replay.
- Removing/changing a binding can retarget unchanged writers; all remaining
  roots participate rather than only roots listed against an old final handle.
- Reordering Project can change winners without changing Source bytes.
- Header enum/alias/type changes can change results of unchanged Source.
- Numeric file ID, scheduler order and physical Graph placement never replace
  logical event order. Events from different snapshot domains are not compared.

Future sparse replay requires a proven dependency closure and tests for these
cases. Production's current sparse BUILD/invalidation remains unchanged and is
not automatically correct for this new semantic policy.

## 5. History/persistence/BUILD boundaries

Current retained values/events/origins are useful provenance but do not form a
complete symbolic replay log. Endpoint ordinals lost their endpoint arena in
PLS07-A. A canonical target under yesterday's bindings is not a substitute for
the original qualified name/selectors when bindings change.

An optional future cached Replay Input would need original declarations/type
requests, symbolic endpoints and scope/visibility context, ordered operations,
origins, preprocessing/Header context and explicit invalidation revisions.
Neither SourceMap nor the private ordinal fields satisfy that contract.
No such cache/log is introduced now. PLS07-A payload stays intact until a
separate representation experiment explicitly chooses what to retain.

Full semantic replay does not automatically implement stable-WHERE BUILD:
surviving WHO must remain in the existing identity lineage, and publication must
reconcile object/link/path/provenance handles under the chosen persistence
contract. If that cannot be preserved, return an incompatible-lineage/rebuild
requirement rather than silently calling a fresh physical PUBLISH an incremental
BUILD. No reconciliation or new BUILD entry point is implemented in B1.

Before persisting the new pipeline, artifacts must identify its semantic policy
and compatible loader/provenance/replay contract. Existing source.bin dependency
observations and compiled.bin final arrays cannot be relabeled as replay input.
Old V2 and alias-aware conflict fixtures intentionally have different outcomes;
semantic equivalence is mandatory, universal old-byte parity is not the new
policy's correctness definition.

## 6. Evidence from current code

| Existing point | Implication for contract |
| --- | --- |
| `graph.hpp`: `add_resolved_object`, `complete_resolved_extern_object` | Object registration/type/state currently lives outside the result; do not double-register in writer |
| `graph.hpp`: `intern_resolved_endpoint_path`, `add_resolved_initialization`, `rebind_resolved_link` | Existing resolved APIs can be investigated by first backend without reparsing Source or changing production ABI |
| `source_parser_v2.cpp`: `commit_queued`, `commit_construction` | Current parser commits G and SourceMap together; new writer must explicitly preserve provenance after assigning new handles |
| `source/source_map.hpp` | Root-contiguous final provenance and BUILD dependency observations are separate from G/event history |
| `runtime/project_runtime.cpp`: objects, links, initializations phases | Baseline construction and final Source overlay need ordered materialization semantics |
| `project_build.cpp`: persisted-initialization invalidation and `parse_semantic_roots` | Current sparse replay is a production algorithm, not proof of correctness for changed final-cell semantics |
| `PLS07-A ownership.hpp` | Values/provenance have owned storage; object/type catalog and complete symbolic replay input are still absent |

## 7. Executable checks and limits

`FinalWriterContractTests` compiles the **actual PLS07-A owned_result** with the
new adapter, using a synthetic matching catalog. It checks selected-state access
independent of archived endpoint ordinals, losing-history exclusion from writer
API, empty whole-object paths, binding direction, extern provenance, distinct
logical root/physical operand file, Source-initializer classification, wrong
domain/catalog, missing object, invalid ranks/files/cells, duplicate final values/
bindings, residual dereference paths and warning-order rejection. Projection
readiness is rejected for materialization. An empty Source replay plan is valid.

`verify_replay.py` uses the existing PLS07 executable for ten full fresh
parses: before/after winner removal, before/after rebind removal, both Project
orders, before/after Header enum change, rejection of a missing still-listed
Source, and rejection of a surviving write whose object declaration is gone.
It verifies final values/warnings,
ownership invariants and zero token reads in global linking. It does **not**
invoke production incremental BUILD, reuse old artifacts or test stable-WHERE
reconciliation. Results are under `build/parallel-local-source-07-b1/replay-policy/`.

Both CTest suites pass. No performance benchmark is warranted: this stage adds
an experimental interface/checker, not a faster production path. The optional
cold checker allocates diagnostic bitmaps/sets and does not revalidate types or
layout; it is not a proposed per-insert hot-path validation layer.

Reproduction: `experiments/parallel-local-source-07-b1/README.md`.

## 8. Next implementation gate

After this contract, a compact final representation may be measured behind the
same reader API. Keep warning/provenance and the replay policy intact. Then
implement an experimental backend against a valid seed, with independent G,
query, LOAD and Runtime oracles for alias conflicts, declaration initializers,
Header defaults, references, arrays and diagnostics.

Measure the complete path: acquisition/Headers/lexing, extraction, linking and
object registration, alias reduction, ownership, final writer/SourceMap,
persistence, Runtime/SHM publication and cleanup. Include seed construction;
do not compare a writer-only timer to production PUBLISH or add medians from
different experiments. Parallel extraction remains a later independent test.
