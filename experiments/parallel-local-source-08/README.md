# PLS08 — Experimental PUBLISH Graph Writer

PUBLISH-only standalone experiment. No production PUBLISH/BUILD/REBUILD/LOAD
changes, worker pool, compact ABI, new WHO/type database or Graph format.

```powershell
cmake -S experiments/parallel-local-source-08 -B build/parallel-local-source-08
cmake --build build/parallel-local-source-08 --config Release --parallel 8
ctest --test-dir build/parallel-local-source-08 -C Release --output-on-failure
python experiments/parallel-local-source-08/run_real.py build/parallel-local-source-08/unitproxl
```

Build compiles current engine core from source, Release+IPO. The driver is a
generated isolated PLS07 copy with Header V2 provenance enabled. Scripts/tests
set PLS03 flat interning, PLS04 early release, PLS06 dense heads, PLS07 ownership
and correctness checks. Direct use requires `PLS03_MODE=1`, `PLS04_EARLY=1`,
`PLS06_DENSE=1`, `PLS07_OWN=1`; `PLS08_REVERSE=1` reverses physical path/link/value
insertion independently of extraction `--reverse`.

`writer.hpp` consumes the B1 reader, fresh Header/object seed G and SourceMap.
It uses existing resolved Graph APIs. No Source syntax/history is read. Native
Root/file provenance includes duplicate contributors reconstructed from ready
warning transitions, not from Parser events.

`runtime_probe.hpp` serializes through the existing compiled-image encoder,
checks readonly compiled query and SourceMap, derives ABI layout, rejects
overlapping final Source ranges, and uses unchanged Runtime V2 materializers
into an **in-process SHM byte image**, including native pointer checks.
This is not named/resident SHM publication. ABI is Windows x64, pack 8.

Only verified private `*.compiled.bin` test artifacts are written beside report
JSON. No runtime.bin/source.bin, server artifact install, publication metadata
or project `.serverengine` output is created/overwritten. Legacy oracle is read
only. Assign inputs are explicitly unsupported in this feasibility slice.

Native const declaration initializers use the existing matching cold object
record. Const qualifiers are never stripped to bypass the mutable-init API.
Union/other overlapping physical writes fail closed; no physical-order winner
is accepted. Small-fixture dumps use raw scalar bits and SHM-relative pointer
targets, and are limited to fewer than 100 objects/arrays up to 128 elements.

The JSON field `semantic_projection_lifecycle_ms` retains the earlier semantic
timer and excludes writer/probe work. `graph_writer.full_publish_benchmark` is
always false. Component/validation times include cold checks and are not a
production PUBLISH benchmark, not paired optimization evidence and not additive
stage medians. No performance improvement is claimed.

See [PLS08-A report](../../docs/PLS08_A_GRAPH_WRITER_FEASIBILITY.md).

## PLS08-B integration

Four CTest suites cover 139 fixture invocations. `test_resident.py` additionally
checks expected values and native pointers in actual resident SHM after native
PUBLISH, including compiled.bin/runtime.bin persistence. All native test outputs
are confined to the build fixture tree. The diagnostic probe now checks every
Header/Source root and inverse physical-file SourceMap index, not just Source
winners. See [B report](../../docs/PLS08_B_INTEGRATION_PARITY.md).

## PLS08-C full PUBLISH A/B

`PublishFinalState08` compiles a generated copy of the native parser orchestration
and benchmark. A runtime selector chooses unchanged Source V2 (`0`) or the
experimental final-state pipeline (`1`) in the same executable. The normal native
Header, persistence, runtime.bin encoding, resident SHM and cleanup paths run in
both arms. The candidate skips unused V2 Source predecode overlap; extraction
remains sequential. No workers have been added.

```powershell
python experiments/parallel-local-source-08/run_publish.py build/parallel-local-source-08/publish-c-new --pairs 15
```

The runner requires a NEW directory under build, copies the input fixture without
existing server artifacts, and freezes a private executable. It first runs the
deep standalone materialization oracle, compares all resident values/references,
and audits complete object storage/default bytes against V2. These are cold
checks, disabled during timed repetitions. SHA-256 only runs in the cold verifier
or Python after operation timers, never as Source acquisition hashing.

Timed output distinguishes native operation latency (`total_ms`) and operation
plus resident teardown (`pls08_full_with_teardown_ms`). The latter adds only the
measured teardown duration; report formatting/cold checks are excluded. Normal
construction cleanup is already included in native operation latency. Process
wall time is recorded separately. Phase-only profiles run after the A/B series.
Each arm must keep its own compiled/runtime digest stable; cross-arm old byte
parity is intentionally not required under the new Source contract.

`PLS08_CANDIDATE=1` selects the experimental hook. `PLS08_COLD_CHECK=1` dumps small
resident fixtures; `PLS08_EXPECTED_IMAGE` validates all values/references against
a verified image; `PLS08_OBJECT_AUDIT=1` hashes all resident object storage.
These last three flags must be absent from timed runs. CLI supports PUBLISH
modes only. Assign and Source paths through union storage are rejected by the
candidate. Never point this native lifecycle executable at a production project:
it runs normal PUBLISH artifact replacement in its supplied project directory.
