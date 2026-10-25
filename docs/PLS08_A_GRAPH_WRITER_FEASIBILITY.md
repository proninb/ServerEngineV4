# PLS08-A — Experimental PUBLISH Graph Writer Feasibility

## Result and scope

Existing resolved Graph APIs can materialize the checked final-cell Source
projection into a real G and native SourceMap. The resulting native-format
compiled image passes readonly query/integrity checks and drives existing
Runtime V2 materializers successfully on UnitProXL in both physical orders.

This closes the first PLS08 feasibility slice. It does **not** close complete
production semantic coverage, resident publication or PLS08-C full PUBLISH
benchmark. No production path, BUILD/REBUILD/LOAD, replay policy implementation,
worker count, compact ABI or Graph format was changed.

## Implemented path

```text
Source acquisition / Header V2 with existing provenance sink
  → PLS07 local extraction (same sequential scheduling)
  → global linking + object registration in seed G
  → alias reduction + dense heads + owned result
  → existing resolved Graph APIs + native SourceMap
  → existing compiled-image encode / readonly query
  → existing ABI layout / Runtime V2 materializers
  → in-process SHM image / independent byte-pointer checks
```

The seed is created once: Header/type/default facts plus registered objects,
without previous Source initialization/link overlay. Header link/init counts
are captured before Source and checked unchanged before writer. Source cold
declaration construction is classified separately from Header defaults.
No second identity/type system or second object-registration pass is introduced.

`pls08::write_supported_projection` is explicitly a feasibility backend. It
uses the B1 reader/typed views but does not forge the general B1 closed-semantics
token or implement its still-opaque production candidate factory. Accepted
inputs are verified through physical materialization before artifacts are saved.

## Graph mapping

For each canonical cell, build one temporary cell-ID → `object_endpoint` entry.
Keep input WHO/cell IDs and owner ranges unchanged. Root/final types come from
the matching seed catalog; no Source tokens, endpoint grammar or full write
history are consulted.

| Final datum | Existing representation/API |
| --- | --- |
| Simple member cell | Existing direct-member `endpoint_ref` encoding |
| Multi-step or empty whole-object path | `intern_resolved_endpoint_path(root_type, steps, value_type, ...)` |
| Explicit final reference binding | `rebind_resolved_link(source, target, ...)` |
| Mutable final scalar value | `add_resolved_initialization(target, resolved_type, value, ...)` |
| Whole-object const declaration initializer | Existing verified cold object construction, under strict conditions below |

Direct-member and a one-step path wrapper are different raw target keys in the
current Graph indices. Writer must not accidentally create a second link for
the same direct member. The established direct-member form is used for that
case; other paths use the resolved interner. No private `member_index`
constructor, Graph friend, storage access or format bypass was added.

Writer runs only after final winners/bindings are chosen. Reverse mode reverses
path interning and physical link/value insertion, independent of extraction
order. Graph handles and bytes may differ; final values/reference topology and
diagnostics cannot. Graph's `replaced` flag does not create new Source warnings.

## SourceMap and provenance

Header V2 is called with its existing optional `set_provenance` sink inside
normal Header root boundaries. Writer appends Source roots after Header roots,
using only ready reader data:

- declaration occurrences add their object WHO contribution;
- winning values add object contribution plus canonical initialization target;
- final bindings add their **new** Graph link handle;
- previous/current warning transitions preserve all duplicate contributor roots
  and physical files against the final datum, including losing writers.

For n writes, the n−1 transitions cover every contributor. Per-root emission is
ordered by existing logical events and deduplicated by native SourceMap APIs.
SourceMap grouping/Graph insertion never chooses the winner.

Verification independently checks each declaration, winner, final binding and
both sides of each warning against native root/file membership and native
initialization-target provenance. It also compares persisted contribution/root
ranges against native SourceMap using the compiled query API. Handles are taken
from this writer's G, never assumed equal to old handles or input cell IDs.

Native SourceMap represents root/file → Graph datum, not every event/range or
the two detailed warning locations. Those fine-grained fields remain in the
owned result/report as before; no new persisted provenance format is invented.
No source.bin is emitted and no compatibility with production BUILD dependency
acceleration is asserted. Root initialization provenance is checked in memory;
not all such acceleration fields belong to compiled.bin's native sections.

## Runtime/query checks

The experiment uses the existing compiled encoder, `compiled_project_view.bind`
and deep `verify_contents`. Every Source winner is queried back through the
compiled initialization index, or through the existing cold object record for
the special const case. Every final binding is queried back through the compiled
target/link index.

Runtime execution uses existing Windows x64/pack-8 APIs in their required order:
prepare layout/model, canonical storage, link markers, object/type construction,
final links, final initializations. Output is a vector-backed SHM byte image with
real native reference pointers; named/resident server SHM is not published.

An independent path walk through the compiled type/layout metadata computes
expected physical addresses. Every selected value is checked byte-for-byte
against its normalized value and ABI width, including floats and enum storage.
Every selected reference is checked against the exact native address of its
final source cell. Small fixture field/default values and pointer relationships
also have explicit independent Python expectations, beyond Graph counts.

Different canonical cells do not automatically mean disjoint memory. Before
Runtime stores, final scalar/reference target ranges are calculated and checked
for overlap. This verification is cold feasibility work, not a proposed new
per-write parser cost or production parallel-store policy.

## Native const declaration boundary

The first independent const fixture exposed a real API boundary:
`graph::scalar_initialization_target` rejects top-level const, so even the
resolved scalar-init API is not a declaration-initializer API.

The existing Graph already stores whole-object const declaration construction
in the object's cold slot. Writer uses that native representation only if:

1. the selected value is explicitly marked as a declaration initializer;
2. the cell is the whole object (empty path);
3. the seed object has the Source non-default-initializer flag/origin;
4. its cold construction exactly matches the selected normalized value;
5. there is no competing explicit initialization record for that endpoint.

The const qualifier is not removed, the resolved API is not passed a fake
mutable type, and no extra format/API is introduced. Query uses the cold record;
Runtime confirms its value. Native SourceMap records object ownership rather
than inventing an initialization-array entry for a value stored elsewhere.
Mismatch or other const-write cases fail with a precise diagnostic.

Additional fixtures verify const integer zero, negative integer and double
negative zero in both physical orders: three cold records, zero initialization
records, correct values/sign bit. Mutable declaration initializers and later
overrides remain explicit final initialization records and override their cold
metadata in effective query/Runtime.

## Exact remaining semantic gap: overlapping storage

The union fixture is deliberately rejected:

```cpp
union U { int a; int b; };
U u;
u.a = 1;
u.b = 2;
```

The current Source canonicalizer gives different member-path cell IDs to `a`
and `b`; ABI layout puts them in overlapping storage. A physical insertion
order would choose a result despite the producer having claimed final state.
PLS08-A refuses this with `Overlapping final Source storage ranges require
semantic canonicalization` before Runtime execution/artifact writing.

This is a supported-domain/canonicalization gap, not a reason to add a Graph
format or silently merge by raw offset inside writer. Such cases require a
separate semantic decision before extending accepted inputs. Whole UnitProXL
has disjoint final Source scalar/reference ranges under the checked ABI.

## Tests and real-corpus result

Three CTest suites pass: **94 fixture invocations** in total:

- 46 existing positive/negative semantic/origin/diagnostic cases, now invoking
  writer/query/Runtime for the positive cases;
- 44 independent Runtime cases: aliases, rebind, declaration init/override,
  Header/native defaults, strings, enum/float/const, inheritance, cross-root
  contributors, empty Source, with both extraction and physical writer orders;
- two cold-const Runtime cases and two overlap rejections without a saved image.

UnitProXL runs forward and with reversed extraction + physical writer insertion.
Both pass the existing independent V2-compatible Source projection oracle before
writing, plus all new G/query/SourceMap/Runtime checks afterward:

| Metric | Forward | Reverse |
| --- | ---: | ---: |
| Objects | 137,584 | 137,584 |
| Graph links / checked final Runtime references | 367,144 | 367,144 |
| Graph initializations / checked final Runtime values | 705,238 | 705,238 |
| Cold-only const declaration cases on this corpus | 0 | 0 |
| Disjoint final Source scalar/reference ranges | 1,072,382 | 1,072,382 |
| Native SourceMap contributions including Header | 1,268,144 | 1,268,144 |
| Independently checked Source provenance occurrences | 1,209,966 | 1,209,966 |
| Compiled semantic image bytes | 149,051,912 | 149,051,912 |
| In-process SHM image bytes | 2,146,276,800 | 2,146,276,800 |
| Input semantic fingerprint | 13649286433448276567 | 13649286433448276567 |

Initializations include the explicit declaration occurrence introduced by the
new Source model; the old projection oracle checks its compatible subset and
is not an old-byte/runtime oracle for changed alias-conflict semantics. Fixtures
have explicit expected outcomes for those intentional differences.

Only verified private `*.compiled.bin` artifacts are written beside experiment
JSON. Nothing is installed into project artifacts or server state. No resident
publication, runtime.bin/source.bin production persistence, Assign user-data
coverage or production LOAD/query integration is claimed. Assign inputs are
rejected explicitly in this feasibility slice.

## Timing is not PLS08-C

This driver performs deep integrity verification, SourceMap set reconciliation,
per-value expected-byte allocation/checks, history checks and serial Runtime
materialization into a fully zero-initialized ~2 GiB vector. Those activities
are not the production hot publication path.

The two validation processes took about 13.36 / 12.98 seconds; those are **test
process durations, not PUBLISH latency**. One-off component traces were writer
together with SourceMap finalize 440 / 419 ms, encoding + cold checks roughly 1,040 / 961 ms,
and Runtime preparation/materialization with range checking roughly 1,024 / 976
ms. They are neither paired optimization measurements nor additive full-path
medians and must not be subtracted from old production PUBLISH.

`semantic_projection_lifecycle_ms` explicitly retains the previous semantic
timer and excludes writer/probe work. `graph_writer.full_publish_benchmark` is
always false. There is no claim of speedup or a subsecond new PUBLISH.

## Next gate

PLS08-A establishes materialization feasibility on the checked supported input
domain using existing Graph/SourceMap APIs and Runtime code. Before PLS08-C,
complete the remaining PLS08-B integration oracles and wire an isolated full
PUBLISH driver with the real production-equivalent acquisition/seed costs,
artifact transaction, runtime.bin/native physical columns, resident SHM and
cleanup, with correctness checks separated from timed repetitions.

Keep current production code unchanged and compare full runs in the same
environment. Do not add workers, compact ABI or a new format to mask gaps.

Reproduce using `experiments/parallel-local-source-08/README.md`.
Fixture reports/images are under `build/parallel-local-source-08/`; real corpus
reports and two verified images are in its `unitproxl/` directory. Both current
Source/Runtime APIs and existing private compiled format are used unchanged.
