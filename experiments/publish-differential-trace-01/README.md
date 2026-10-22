# PUBLISH-DIFFERENTIAL-TRACE-01 / COVERAGE-CONTRACT-01

Read-only differential tooling. The experiment does not integrate the new Parser
or evaluator into production. `generate.py` creates instrumented copies of six
engine translation units under the build directory; original engine files are
never edited. Unmodified engine objects are imported from the SB03 Release build.
The exact production semantic reuse cache is extracted from parser.cpp for the
Header oracle, rather than approximated by a new cache implementation.

```powershell
cmake -S experiments/publish-differential-trace-01 -B build/publish-differential-trace-01 -G "Visual Studio 18 2026" -A x64
cmake --build build/publish-differential-trace-01 --config Release --target PublishDifferentialTrace01 -- /p:BuildProjectReferences=false
ctest --test-dir build/publish-differential-trace-01 -C Release --output-on-failure
build/publish-differential-trace-01/Release/PublishDifferentialTrace01.exe build/publish-subsecond-01/fixture/project.json build/publish-differential-trace-01/unitproxl 8 32768
```

Native CLI: project, output directory, evaluator workers, chunk target. Frontend
pool remains 16 workers. It compares forward/reverse extraction and three local
task orders. It separately runs the production Header oracle with tracing enabled
and disabled, comparing acceptance, diagnostics, string/WHO tables, Header G
domain bytes and SourceMap observations. Traced timings are not performance data.

Contract suite: 36 fixtures × five evaluator counts × two targets = 360 runs.
Every native run includes the scheduling comparisons. Across configurations,
production trace sequences must remain identical. Known semantic differences are
reported as data; they are not suppressed by making the test suite fail at the
first difference. Positive and negative controls must still match. Coverage CSV
and JSON distinguish acceptance/diagnostic mismatch from partial projection.

Outputs, both per fixture/configuration and aggregated at w8/target32768:

- `string-id-events.csv`: production intern calls, including repeated hits;
- `type-id-events.csv`: production `derive_resolved` calls;
- `semantic-events.csv`: identity resolve calls, parser entries, diagnostics,
  final record/member/base metadata, and projection declarations/requests/operations/WHO;
- `provenance-events.csv`: SourceMap calls and begin/end of actual reuse callbacks;
- `diagnostics.csv`: first production and projection diagnostic, including origin;
- `coverage-summary.json` and `coverage.csv`: observed compatibility status.

CSV `sequence` is a process-global monotonic observation order shared across the
four event streams, not a replacement for semantic decision order. `root` is the
actual file_id of the current Header root (zero for post-pass snapshots). Columns
`a/b/c/text` are event-specific:

| kind | a | b | c | text |
| --- | --- | --- | --- | --- |
| intern | returned string_id | 0 | 0 | spelling |
| derive | child type_ref | derived kind | returned type_ref | numeric payload |
| identity_resolve | parent WHO | name string_id | returned WHO | identity kind |
| parse_* | physical file_id | token offset | token length | empty |
| diagnostic | physical file_id | offset | diagnostic kind | message |
| reuse_begin | reuse event kind | physical file_id | value | empty |
| reuse_end | reuse event kind | physical file_id | reused result | empty |
| source_add | physical file_id | source_data_ref raw | 0 | empty |
| source_dependency | WHO | 0 | 0 | empty |
| final_record_metadata | WHO | record kind | flags | empty |
| final_member_metadata | name string_id | type_ref | access | empty |
| final_base_metadata | base WHO | access | flags | empty |
| declaration | original semantic event | local symbol | declaration kind | name |
| member_request/base_request | original event | local owner | request index | member |
| operation | original event | operation kind | gap kind | target |
| request_origin | physical file_id | offset | length | member |
| numbered_who | local symbol | final WHO | original creation event | name |

Enum values come from the engine/projection headers. Intern/derive/resolve rows
record calls, not only first creations; returned zero denotes failure. Consumers
must not reinterpret every repeated row as a new assignment. Production parse
entries and projection operation records have different granularities and are
not claimed to be identical chronological streams.

Projection has no production string_id numbering, graph-local derived IDs or
SourceMap/reuse implementation. These domains explicitly emit `not_emitted`;
they cannot pass order parity. `input_preparation/prepared_table_snapshot` is the
already-interned input table, not a projection numbering algorithm or intern-event
trace. It demonstrates numbering drift across roots when Parser-created nested
paths precede later physical-file interning. No invented ID sequence is generated.

`projection_parity` proves only the existing type/value projection. In particular,
access/base flags are absent even when acceptance and values match. Partial
callables/initializers are explicitly labelled `partial_projection`. A missing
generic diagnostic detail is a diagnostic mismatch, never exact parity.

## Separate production profile

Build the current production benchmark freshly, with default engine options and
without trace instrumentation. Do not use the trace executable for PUBLISH timing.

```powershell
cmake -S . -B build/publish-end-to-end-profile-01 -G "Visual Studio 18 2026" -A x64 -DSERVER_ENGINE_BUILD_BENCHMARKS=ON
cmake --build build/publish-end-to-end-profile-01 --config Release --target ServerEngineV4PublishBenchmark -- /m:4
python experiments/publish-differential-trace-01/run_profile.py build/publish-end-to-end-profile-01/Release/ServerEngineV4PublishBenchmark.exe build/publish-subsecond-01/fixture/project.json build/publish-end-to-end-profile-01/results
```

Run performance outside sandbox, without concurrent builds/tests/trace runs. The
driver rotates ordinary PUBLISH and coarse timeline, excludes one warmup pair,
measures eight pairs and verifies both frozen artifact hashes after every call.
It does not claim a candidate speedup. Physical Header preparation is separate;
effective preprocessing remains inside the Header semantic timer. Graph mutations
remain inside Header/Source timers; there is no exclusive Graph-write timer here.
Persistence and resident runtime publication are outer disjoint phases. Their
internal subtimers must not be added to the outer accounting a second time.
