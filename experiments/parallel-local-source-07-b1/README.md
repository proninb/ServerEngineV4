# PLS07-B1 — Final Writer Contract

Contract/checking only; no writer backend, compact format or production changes.

```powershell
cmake -S experiments/parallel-local-source-07-b1 -B build/parallel-local-source-07-b1
cmake --build build/parallel-local-source-07-b1 --config Release
ctest --test-dir build/parallel-local-source-07-b1 -C Release --output-on-failure
```

The small C++ test generates current PLS07 header adapters and uses the real
`owned_result` API with a synthetic catalog. It does not import/link stale core
objects or rebuild production. Release checks use explicit exceptions, not
disabled assertions. The separate Python suite needs the existing PLS07 exe
(CMake `PLS07_EXE`, default workspace build path) and performs ten tiny full
Source replay scenarios. If that executable is absent, build PLS07 first; only
the C++ contract suite is configured otherwise.

`final_writer_contract.hpp` defines logical typed views/concepts, a cold
diagnostic checker, replay request/policy and a **declaration** of
`write_final_state`. No implementation of that operation or `candidate_seed`
factory exists yet. Views are not a new storage/serialization ABI.

`owned_reader.hpp` borrows current owner/catalog/Source plan and reads only
selected values/bindings, cells, declarations and ready diagnostics. Prototype
readiness defaults to `projection_only`; diagnostics can inspect it, a writer
cannot treat it as materialization-ready. Catalog/domain/Source plan consistency
is a producer obligation, not a cryptographic proof from the checker.

Replay v1 reacquires/re-extracts all remaining Source after relevant changes,
with matching Header/preprocessing context. The fast final writer has no history
or replay-input argument. This does not implement incremental BUILD or prove
stable-WHERE reconciliation. No PLS07-A history is deleted in this stage.

Full decisions, required fields, defaults/SourceMap/Runtime gates and test limits:
[PLS07-B1 contract](../../docs/PLS07_B1_FINAL_WRITER_CONTRACT.md).
