# SOURCE-COST-BREAKDOWN-01

Read-only diagnostic build of the current Source V2. No production algorithm,
Graph, persisted format, visibility, commit order or diagnostic rule is changed.
`generate.py` instruments copies of source_parser_v2.cpp and its resolver header
under build/. Every function anchor must match exactly once. Balanced API call
wrappers preserve arguments, return values and original call order.

Build the fresh default production benchmark first, following
`../publish-differential-trace-01/README.md`. This experiment imports its Release
objects, including the original benchmark main, and replaces only Source V2.
Its compile definitions match that default configuration: ready Source tokens,
parallel prehash, string batching and string-path reuse ON; model/window/overlap OFF.
Do not import objects from another configuration without adapting this contract.

```powershell
cmake -S experiments/source-cost-breakdown-01 -B build/source-cost-breakdown-01 -G "Visual Studio 18 2026" -A x64
cmake --build build/source-cost-breakdown-01 --config Release --target SourceCostBreakdown01 SourceCostBreakdown01Tests -- /p:BuildProjectReferences=false
ctest --test-dir build/source-cost-breakdown-01 -C Release --output-on-failure
python experiments/source-cost-breakdown-01/run.py build/publish-end-to-end-profile-01/Release/ServerEngineV4PublishBenchmark.exe build/source-cost-breakdown-01/Release/SourceCostBreakdown01.exe build/publish-subsecond-01/fixture/project.json build/source-cost-breakdown-01/results
```

Run native performance outside sandbox, without concurrent builds/tests. The
driver rotates four variants over eight measured blocks, after one excluded
warmup block: baseline production, instrumented executable off/counts/timed.
All execute whole PUBLISH through the same coarse publish-timeline interface.
It verifies compiled/runtime golden SHA256 after every call. The driver does
not modify the original UnitProXL project; pass only its disposable build copy.

`CW_SOURCE_COST_MODE=off` disables timers and counters, but the instrumented
binary still has extra branches, wrappers and changed code generation. It is
not identical to production. `counts` counts calls without clocks. `timed`
adds nested scope clocks and inclusive/exclusive accounting. Counter storage
is thread-local; the existing canonical Source owner performs the measured
semantic work. Existing parallel token preparation remains unchanged.

stderr rows: `SOURCE_COST,category,calls,inclusive_ns,exclusive_ns`. These are
emitted once, after the Source pass, including failed valid-prefix passes.
The root scope encloses local storage destruction. Probe emission itself is
outside that root scope but inside full PUBLISH. No per-event file output occurs.

| Category | Boundary |
| --- | --- |
| source | Entire parse_source_roots_v2, including preparation/release |
| cursor | advance and peek_kind; includes input.next and its interning |
| binding | find_visible and Source identity resolve calls |
| object_lookup | resolver object_type: G.find_object followed by G.find |
| member_transition | resolver prepare/append member, array_type/append_index |
| endpoint_syntax | read_endpoint; inclusive of cursor/resolver/dependency children |
| path_intern | G.intern_resolved_endpoint_path calls, including strings |
| graph_write | object/extern, scalar/link, and batched string Graph writes |
| provenance | SourceMap begin/end root, add, add_dependency, add_initialization |
| declaration_syntax | object declaration handler |
| scalar_syntax | scalar literal parser |
| string_syntax | string statement handler |
| assignment_syntax | assignment handler |

Inclusive scopes overlap. Sum **exclusive** times to the source root inclusive
time; the driver asserts this identity in every timed call. Root exclusive time
is unclassified preparation/scope dispatch/release and probe bookkeeping, not
"pure Parser". A member step can invoke multiple primitive transition methods;
call counts are not counts of unique fields. Batch Graph calls are not counts
of the character records within the batch.

Observer control is essential: complete fine-grained clocks materially change
execution time. Exclusive accounting prevents double counting, but does not
remove timer/counter/code-layout cost. Do not scale these fractions onto the
production Source timer or subtract a guessed per-clock constant. Results are
an instrumented cost map and reliable call-volume census, not an unbiased
production CPU profile. No optimization speedup is claimed.

Tests cover accounting (including recursive same-category scopes and exceptions),
returned references, scalar/reference/string/nested writes, extern/namespaces and
exact errors after valid prefixes. Nine fixtures × baseline/off/counts/timed = 36
checks. Artifact comparison is byte exact for successful fixtures; negative
stderr comparison filters only the SOURCE_COST rows.

Raw logs, measurements.csv, costs.csv and summary.json remain under build/.
The report is `docs/SOURCE_COST_BREAKDOWN_01.md`. Production source files and the
earlier user-visible write-history proposal remain untouched by this experiment.
