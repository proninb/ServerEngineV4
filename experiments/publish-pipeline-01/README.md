# PUBLISH-PIPELINE-01

Standalone pipeline experiment; production engine sources and CMake are untouched.
Results: [PUBLISH_PIPELINE_01.md](../../docs/PUBLISH_PIPELINE_01.md).

The controlled default is **compact**: preserve the existing 24-lane Link mark
and Object executor, stage final immutable Link/Init columns, then overlap ABI/Type
serialization with unpublished SHM construction. One prepared Type plan is shared
without payload copies. The completed G writer mapping is released before the
resident G mapping is opened. Existing physical-plan workers are reused.

Three same-binary modes, selected by `PUBLISH_PIPELINE_01_MODE`:

- `baseline`: original packed persistence followed by resident materialization.
- `serial`: prepared-plan reuse, with writer and consumer sequential.
- `overlap`: identical reuse/staging, with writer and consumer concurrent.

Use a **new** output directory; `--refresh` is only a development convenience and
must never be used on a frozen experiment after timing has started.

```powershell
python experiments/publish-pipeline-01/prepare.py build/publish-pipeline-01-new
cmake -S build/publish-pipeline-01-new/source -B build/publish-pipeline-01-new/build -G "Visual Studio 18 2026" -A x64 -DBUILD_TESTING=OFF -DSERVER_ENGINE_BUILD_BENCHMARKS=ON -DSERVER_ENGINE_ENABLE_IPO=ON
cmake --build build/publish-pipeline-01-new/build --config Release --target ServerEngineV4PublishBenchmark --parallel 4
python experiments/publish-pipeline-01/test.py build/publish-pipeline-01-new
python experiments/publish-pipeline-01/prepare_inputs.py build/publish-pipeline-01-new
python experiments/publish-pipeline-01/run.py build/publish-pipeline-01-new
python experiments/publish-pipeline-01/report.py build/publish-pipeline-01-new
```

`prepare_inputs.py` accepts optional self-contained fixture directories after the
output directory: synthetic, then UnitProXL. Both must contain project.json and
all sources/includes below that directory. It reads/hashes them without mutation;
separate artifact directories are injected by the same private layout shim.
Default fixtures are the retained common corpora:

- `build/direct-g-mmap-ab-01/corpora/synthetic` (10,001 types).
- `build/direct-g-mmap-ab-01/corpora/unitproxl` (24,027 types, 137,584 objects,
  367,144 links).

No builds/tests or other performance work should run concurrently with run.py.
The runner keeps all 15 rotating three-arm native blocks and 15 coarse-profile
blocks per corpus (180 timing observations), warmups, whole compiled.bin and
runtime.bin hashes on every operation, and PUBLISH/audit/LOAD raw SHM hashes
before/after the series. File/SHM hashing is outside operation timing. Native
resident execution has its per-operation telemetry disabled.

Private failure hooks test allocation, encoding, compact setup, resident and
publication boundaries. Tests include a failure after completed SHM; before
returning from the same process they require null public output, absent artifacts
and successful creation of the same SHM name at the same fixed VA. The fallback
path is forced with `PUBLISH_PIPELINE_01_FORCE_SERIAL=1`.

The experimental helper friendship exists only in copied headers. Generated
sources do not add a WHO cache, semantic model, Parser modification or v27 G mmap.
Borrowed spans/plans remain alive through both consumers; no worker mutates Type
operations or file Execution columns. Only separate link-resolution scratch and
SHM are mutable. The incomplete runtime file is never exposed as a valid final
image; the owner commits only after final bind and both joins.

The initial owned-plan pilot is reproducible with `--owned`, but uses the older
owned execution representation with serial link marking. It is diagnostic, not a
controlled overlap-only comparison. Its exact sources/binary/data are retained in
`build/publish-pipeline-01-final/`. The corrected compact study is retained in
`build/publish-pipeline-01-compact/`; do not mix their samples.

Conclusion: no reliable native full-UnitProXL PUBLISH gain; no production adoption.
The overlap metric covers the complete resident worker, including opening and
checking G, rather than an isolated interval of SHM materialization. The remaining
independent ABI/Type writer is only about 1.76 ms in this slice.
