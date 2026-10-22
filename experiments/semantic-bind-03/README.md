# SEMANTIC-BIND-03 — Compact Storage & Lifetime

Three independent representation policies; no new Header semantics, scheduler or production PUBLISH integration. `request` remains 120 bytes. A fully columnar 16-byte request is deliberately not mixed into this experiment.

## Build and validate (MSVC x64)

```powershell
cmake -S experiments/semantic-bind-03 -B build/semantic-bind-03 -G "Visual Studio 18 2026" -A x64
cmake --build build/semantic-bind-03 --config Release --target SemanticBind03Engine --parallel 8
# Engine objects are already built; avoiding recursive project-reference builds
# also avoids an observed MSBuild project-reference stall in this workspace.
cmake --build build/semantic-bind-03 --config Release --target SemanticBind03Timing SemanticBind03Allocations -- /p:BuildProjectReferences=false
ctest --test-dir build/semantic-bind-03 -C Release --output-on-failure
python experiments/semantic-bind-03/run_matrix.py build/semantic-bind-03/Release/SemanticBind03Timing.exe build/semantic-bind-03/Release/SemanticBind03Allocations.exe build/semantic-bind-01/results-unitproxl/SemanticBind01.exe build/publish-subsecond-01/fixture/project.json build/semantic-bind-03/new-results 5
python experiments/semantic-bind-03/analyze.py build/semantic-bind-03/new-results
```

The frozen SEMANTIC-BIND-01 executable is a bridge control, not the baseline used to attribute individual policy effects. Native performance requires execution outside the sandbox and no concurrent builds/tests. The driver rotates/reverses configuration order, executes fresh sequential processes, excludes one warmup per variant/worker configuration and keeps allocation instrumentation separate. Defaults: workers 1/16, five timing repeats, plus separate allocation diagnostics. This is a storage experiment; it does not reselect the scheduler.

## Independent policies

| Variant | Sparse diagnostics | Shared shapes | Segmented requests |
|---|---|---|---|
| baseline | no | no | no |
| sparse | yes | no | no |
| shared_shape | no | yes | no |
| segmented | no | no | yes |
| combined | yes | yes | yes |

Reviewable implementations are split into `diagnostics.hpp`, `bound_storage.hpp` and `request_storage.hpp`; common wiring and unchanged semantic rules are in `model.hpp`. The local Parser is included directly from SEMANTIC-BIND-01. Each single-factor variant is measured separately before interpreting the combined result.

`baseline` preserves dense per-request issues, owning bound-type modifier copies and contiguous request materialization. All new variants share one batch-level request-arena abstraction, range traversal and common template dispatch. This adds a small common infrastructure cost relative to the frozen SEMANTIC-BIND-01 executable; the legacy bridge quantifies it. Do not attribute that common difference to one policy.

### Sparse diagnostics

Each Binding task keeps one local earliest issue and writes its slot once. Slots are reduced in task/index order. Equal-event ties preserve the same first-request choice as the dense reference. Declaration compatibility minima remain part of the same deterministic first-error reduction. This does not change the set of type checks; per-request failure history is intentionally not retained because the supported API consumes only the first error.

### Shared immutable shape

The bound result stores an 8-byte resolved leaf (entity + intrinsic tag), not a 32-byte owning `bound_type`. Modifier sequences remain in the frozen original requests. A shared owner is held **once per binding result**, never once per request; copies/moves retain the owner. Views returned by `operator[]` have ordinary borrowed-span lifetime rules.

The result owns the request arena until its last consumer. It can survive destruction of the batch and worker outputs. This guarantees modifier lifetime, but also pins all request records in that arena. It does not transfer ownership of input spelling text; the exposed result view contains numeric leaf/modifier data only. A future separate shape arena/columnar representation is a different experiment, not silently assumed here.

### Segmented storage

After translating local scope/owner/definition references, combine moves each nonempty worker request vector buffer into the frozen arena. No second full vector of requests is built. Canonical symbols and declarations still have ordered global arrays, as before.

Binding ranges locate a segment once, then traverse contiguous elements. Definition-inventory traversal is sequential too. Random request indexing and result-view access can still use a segment-prefix binary search; their cost must be measured separately for downstream consumers. Existing local vector spare capacity is retained; segmented storage may increase steady-state retained bytes while reducing peak/copy cost. No shrink-to-fit pass is hidden in this policy.

## Validation and lifetime

Each executable runs 270 model checks for each of five policies (1350), plus tests for:

- destroying batch/locals before reading shared shapes;
- moving/copying results and releasing one consumer while another survives;
- allocation churn after the source/function frame is destroyed;
- equal-event negative diagnostic ties across worker counts;
- crossing empty/nonempty segment boundaries and all subranges of a fixture;
- rejection of mutation after the arena is frozen.

`test_variants.py` reuses the independent Header V2 fixtures and adds diagnostic-tie cases: 320 positive/negative oracle comparisons, plus ten allocation/lifetime fixtures. Every real-corpus observation independently verifies WHO, normalized field types and the same partial Header projection with production Header V2. Constructors/defaults/static objects/full diagnostics are not newly supported.

## Metrics and audit

`model_total_ms` has the same boundary as SEMANTIC-BIND-01: local extraction, ordered assembly/forest preparation, exact-key agreement, type binding and deterministic numbering, including normal local cleanup/result copies. Input preparation/preprocessing, pool startup, oracle, retained-output destruction and downstream result consumption are excluded.

Primary capacity bytes reconstruct the old 145.7 MiB subset: symbols, declarations, requests, request modifiers, bound records/modifier copies and diagnostics. Arena/segment descriptor metadata are reported separately; shared control-block overhead, adjacency and temporary maps are not included in this subset. Global tracked requested-byte peaks in allocation mode include preparation and pool, and exclude diagnostic allocator headers/native heap overhead/mapped bytes. Never compare allocation-mode duration with native timing.

`lifetimes.csv` captures request instances at parse/combine/local release/binding/numbering boundaries. `allocations.csv` includes PMR requests through the existing diagnostic hook and must return to zero live bytes at final teardown. Runtime policy selection is immutable during each parse/bind operation.

## Result-only consumer audit

Build separately **after the native matrix**, reusing the exact engine object directory:

```powershell
cmake -S experiments/semantic-bind-03/consumer -B build/semantic-bind-03/consumer -G "Visual Studio 18 2026" -A x64 -DENGINE_OBJECT_DIR=C:/Boris/ServerEngineV4/build/semantic-bind-03/SemanticBind03Engine.dir/Release
cmake --build build/semantic-bind-03/consumer --config Release --target SemanticBind03Consumer
python experiments/semantic-bind-03/consumer/run.py build/semantic-bind-03/consumer/Release/SemanticBind03Consumer.exe build/publish-subsecond-01/fixture/project.json build/semantic-bind-03/new-consumer-results
```

This deliberately measures the current public indexed result-view API in sequential and fixed-shuffle order. Each fresh process first proves Header V2 parity, destroys batch/worker inputs and physical/spelling world, verifies owner survival, performs one excluded checksum traversal per pattern, then five alternating measured traversals. Three processes per policy; all policies must produce identical checksums. After the last consumer releases the result, the arena must expire.

It is a single-thread checksum stand-in, not Direct G or actual PUBLISH. It exposes late access/lifetime costs that the pipeline timer excludes. Result-only capacity can grow in shared-shape variants because the safe owner pins the request arena; do not claim that removing bound modifier copies allows freeing that arena early.
