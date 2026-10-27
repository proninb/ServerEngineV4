# PUBLISH: Parser Graph in final compiled.bin mmap

**Historical implementation note — rolled back.** After DIRECT-G-MMAP-AB-01
showed regressions, the v27 code, allocator, feature flag and counters were
removed from the active source tree. PUBLISH again constructs RAM G and encodes
the packed artifact. The implementation and commands below describe the archived
experiment, not current build capabilities. Frozen sources, binaries and scripts
remain in `build/direct-g-mmap-ab-01/`.
See [DIRECT_G_MMAP_AB_01.md](DIRECT_G_MMAP_AB_01.md) for results.

With the experimental flag enabled, PUBLISH creates the final compiled.bin before semantic construction. The persisted
Graph columns use pmr vectors backed by mapped_file_resource, rather than heap
vectors. Parser mutates those mapped records through the existing Graph API.
REBUILD retains the packed encoder and its durable persistence policy.

## Storage and lifetime

- The resource allocates 64-byte-aligned ranges in compiled.bin. File capacity
  grows in 64 KiB quanta. Every allocation has its own shared writable view.
  Growing one column does not invalidate the views of other columns.
- A vector growing beyond capacity reallocates inside the same file and unmaps
  its previous allocation. This is ordinary construction-time vector growth;
  there is no heap-owned complete G and no final Graph encoding/packing pass.
- Old allocations and unused vector capacity remain as file slack. This format
  deliberately trades a larger artifact for avoiding final payload compaction.
  Construction is not copy-free: vector growth still moves existing elements.
- The file resource outlives G and owns native mapping views. Graph move assignment
  reconstructs the object so pmr allocator ownership propagates with its columns.
- Transient identity/member/link/path lookup structures remain construction
  accelerators. Strings, identities, source and assign metadata retain their
  existing construction/encoding paths; this change concerns Graph payload.

The native persisted records have explicit size and field-offset assertions and
require little-endian storage. Object construction uses one final cell per object
WHERE in mapped mode, including zero-initialized objects. Completing an extern
fills that same cell. The ordinary RAM Graph keeps its existing sparse cells.

## Persistence and format

compiled_project_layout::use_mapped_graph adopts the actual Parser allocation
offsets for types, bases, members, derived types, objects, construction values,
identities of types/objects, links, object initializations, endpoint paths/steps
and constructor defaults. Other sections are allocated at the file tail.

The mapped branch of encode_compiled_project_image writes only metadata and
persisted query indexes. It does not zero, rewrite or copy Graph payload. The
compiled_encode telemetry interval therefore measures metadata/index finalization
on PUBLISH, not a Graph serialization pass. Query index construction still walks
relevant Graph records. ABI layout and runtime Type/link/initialization plans
remain necessary and are built from the compiled view as before.

Format v27 permits arbitrary 64-byte-aligned section offsets and unreferenced
allocation slack. Cold bind checks each section's size and bounds and rejects
all overlapping nonempty sections, including overlaps with the prefix. It does
not require zero-filled gaps. Packed v25/v26 keep their original canonical-order,
padding and exact-end checks. Enum-aware ABI consumers already accept versions
>=26. REBUILD/BUILD continue writing packed v25/v26; LOAD reads all three versions.
There is no added explicit durable flush for PUBLISH.

Failed construction destroys its views before the existing artifact cleanup.
The directory must now be created before Parser, since its G already belongs to
the final artifact. No temporary graph file is created.

## Validation

Windows/MSVC Release validation uses build-validation:

```powershell
cmake -S . -B build-validation -DSERVER_ENGINE_DIRECT_GRAPH_MMAP=ON
cmake --build build-validation --config Release --target ServerEngineV4Tests ServerEngineV4PublishBenchmark ServerEngineV4 ServerEngineV4SourceMapTests --parallel 8
ctest --test-dir build-validation -C Release -R '^(compiled_project|source_map)$' --output-on-failure
```

The compiled_project regression constructs the fixture in the final mmap and
checks arena extension with live earlier views, allocator-preserving Graph moves,
dense extern completion, constructor defaults, endpoint paths, initializations,
runtime plans, original Parser offsets in the directory, unchanged type payload,
byte-for-byte equality of every live section against the packed encoder,
read-only reopen after producer destruction, and overlap/out-of-bounds rejection.
Existing packed image, sparse BUILD and runtime tests remain in the same suite.

End-to-end PUBLISH -> LOAD -> audit was exercised on generated mixed fixtures:
1,001 types / 17,001 members / 2,000 objects / 1,000 links and
10,001 types / 170,001 members / 20,000 objects / 10,000 links. The smaller fixture
also passed REBUILD -> audit -> PUBLISH to exercise packed/mapped transitions.
A separate fixture covered enumeration, inheritance, scalar initialization,
extern completion and char-array endpoint initializations.

```powershell
./benchmarks/new_publish_fixture.ps1 -OutputDirectory build/direct-graph-mmap-scale -Scenario mixed -TypeCount 10000 -FileCount 16
build-validation/Release/ServerEngineV4PublishBenchmark.exe publish-profile build/direct-graph-mmap-scale/project.json 10001
build-validation/Release/ServerEngineV4PublishBenchmark.exe load build/direct-graph-mmap-scale/project.json 10001
build-validation/Release/ServerEngineV4PublishBenchmark.exe audit build/direct-graph-mmap-scale/project.json 10001
```

Fixture generation requires a new output directory. Measurements from this
validation are not an A/B speedup claim and must not be compared with the earlier
200.8 ms series. End-to-end timings must include mmap allocation/page faults now
paid during Parser, as well as the remaining metadata and runtime preparation.
