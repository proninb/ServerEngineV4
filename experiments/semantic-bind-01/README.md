# SEMANTIC-BIND-01 — Header type binding experiment

Standalone executable; ordinary PUBLISH, BUILD, REBUILD and their build options are unchanged.

```powershell
cmake -S experiments/semantic-bind-01 -B build/semantic-bind-01 -G "Visual Studio 18 2026" -A x64
cmake --build build/semantic-bind-01 --config Release --target SemanticBind01 --parallel 8
ctest --test-dir build/semantic-bind-01 -C Release --output-on-failure
build/semantic-bind-01/Release/SemanticBind01.exe build/publish-subsecond-01/fixture/project.json 4 1
python experiments/semantic-bind-01/run_matrix.py build/semantic-bind-01/Release/SemanticBind01.exe build/publish-subsecond-01/fixture/project.json build/semantic-bind-01/new-results 5
```

Run native performance measurements outside the sandbox, without concurrent builds/tests. The matrix refuses to overwrite a result directory. It rotates/reverses worker configuration order, starts a fresh process per observation, excludes one warmup per configuration and verifies every observation against Header V2. Raw output, executable/source hashes, medians, MAD and extrema are retained.

## Boundary and coverage

`model.hpp` consumes effective token kinds, exact spelling views, decoded array bounds and physical source locations. It does not include or call `identity_space`, `string_table` or `graph`. It assigns local numeric symbols to namespace/record/alias declarations and emits type-expression requests with local scope/owner references. Original tokens are not revisited by the Binder. Source buffers/spelling storage must outlive the batch.

Each manifest Header root is one work item; `root ordinal + effective token ordinal` defines semantic event order. Includes and macro substitutions already have effective order and physical origin at this boundary. No global WHO, canonical parent, lookup answer or oracle result is supplied to workers. Outputs are assembled in root order, irrespective of completion order. Empty roots are retained.

The experiment reuses the CANONICALIZER-01 exact-key index and common WHO numbering. It then constructs an immutable name catalog. Parallel requests search the canonical scope and its ancestors, filtering declarations by event. Completeness uses a separate definition-completion event. Alias folding also uses an event: a later intrinsic alias must not retroactively fold an earlier pointer-to-forward-record expression.

Supported projection: namespace scopes/reopening; record declarations/definitions; unqualified named type lookup; intrinsic aliases; base type binding; ordinary scalar, pointer, reference, array and parenthesized pointer/reference-to-array member types. Record/alias compatibility and duplicate definition **field/base type shapes** are checked in deterministic order. Forward-record family bookkeeping follows Header V2's parser-instance scope rather than imposing stronger C++ rules.

**This is not a complete Header Binder.** Callable/constructor bodies and member initializers are counted as semantic exclusions. Constructor reference targets, constructor operations/defaults, static objects, method signatures, access/layout/polymorphism compatibility and full grammar/error coverage are not proved. A successful row says `coverage=partial` whenever callable/initializer exclusions occur. Even with no exclusions, `coverage=type_binding_subset` denotes the explicitly supported projection, not all Header semantics. Unsupported syntax is a failure to prove parity, never a fallback advertised as success.

The independent oracle is the production Header V2 Parser with its own string table, identities and G, executed **after** the candidate. It performs preprocessing afresh, with no experiment token-reuse callback. For positive inputs the harness compares every local WHO, every normalized member type and every base target; it also checks the complete field inventory per unique record and overall member count. It does not compare serialized G, WHERE, String IDs, Source Map or runtime output. Negative fixtures compare rejection category and exact first physical error location (file, offset, length).

## Preprocessing is an explicit cost

`main.cpp` runs the production effective preprocessor sequentially before the experiment. That preprocessor still interns identifier spellings. The worker input contains only final spelling views, not its numeric String IDs; this does **not** demonstrate a new frontend without global interning. Preparation/capture/adaptation and worker-pool startup are reported separately from the model pipeline.

`token_reuse.hpp` admits repeated complete global files only under the production preprocessor's empty-macro/boundary eligibility. Ordered include/pragma-once actions are preflighted and replayed. It carries no semantic identities, G, contributions or binding answers. Tokens from the first admitted execution remain in the batch and are validated later. Reuse under namespaces or active macros is disallowed by the preprocessor. Oracle fixtures exercise both prohibitions and repeated includes. Source Map/provenance replay is outside this experiment.

The first exploratory run used independent expansion for every root: 20,590,702 requests and about 17.5 GB peak process working set. Those numbers describe an inflated stress workload, not PUBLISH. The corrected real-corpus input uses eligible token reuse and has 751,783 requests; final measurements must not be mixed with the exploratory run.

## Metrics

- `local_parse_ms`: parallel local-symbol/type-expression production from immutable tokens; local vector allocation included.
- `combine_prepare_ms`: reserve, ordered concatenation, local-reference translation, forest validation, depth and child adjacency. Modifier vectors are moved, not copied.
- `canonical_ms`: exact-key agreement, including raw-name hashing.
- `binding_ms`: immutable catalog construction, declaration checks, parallel lookup/completeness/alias folding and duplicate field/base shape checks. This includes all Binder setup and allocation.
- `numbering_ms`: unchanged deterministic WHO numbering.
- `model_total_ms`: elapsed pipeline time from allocating worker outputs through copying final representatives/WHO into the returned experiment result. It includes releasing local-output copies and residual bookkeeping; it excludes token preparation, pool startup, oracle/verification and destruction of returned/canonical result storage. Component medians need not sum to the median total.
- `model_capacity_bytes`: requested capacity of the retained primary symbol/declaration/request/result vectors and modifier arrays. **Not** total live or peak temporary memory: maps, adjacency, allocator overhead, worker locals and input buffers are omitted.
- `process_peak_ws_bytes`: Windows process lifetime PeakWorkingSetSize sampled after the candidate, before the oracle for a fresh single-repeat process. Includes preparation/input/allocator costs. Later in-process repeats inherit earlier process peaks; use the fresh-process matrix for memory comparisons.

Workers are forced to 1/2/4/8/16 for measurement. No adaptive scheduler or unmeasured threshold is enabled. No new Direct G, Source Binder or PUBLISH integration is supplied.
