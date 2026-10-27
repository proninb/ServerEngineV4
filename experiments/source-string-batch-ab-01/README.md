# SOURCE-STRING-BATCH-AB-01

Current production V2 + Fast Reader, existing scalar versus batch string Graph
commit in one frozen native executable. Production already defaults batch ON;
this experiment validates that choice, adding no production algorithm.

Result: scalar 1370.42 ms, batch 1331.39 ms. Paired native gain 35.09 ms,
95% CI 21.27–39.96 ms, wins 13/15. Report:
`docs/SOURCE_STRING_BATCH_AB_01.md`.

Use a new output directory:

```powershell
python experiments/source-string-batch-ab-01/prepare.py build/source-string-batch-ab-01-new
cmake -S build/source-string-batch-ab-01-new/source -B build/source-string-batch-ab-01-new/build -G "Visual Studio 18 2026" -A x64 -DBUILD_TESTING=ON -DSERVER_ENGINE_BUILD_BENCHMARKS=ON -DSERVER_ENGINE_ENABLE_IPO=ON -DSERVER_ENGINE_PUBLISH_PGO=OFF
cmake --build build/source-string-batch-ab-01-new/build --config Release --target ServerEngineV4PublishBenchmark ServerEngineV4StringBatchAudit ServerEngineV4FrontendTests ServerEngineV4Tests --parallel 2
python experiments/source-string-batch-ab-01/test.py build/source-string-batch-ab-01-new
$env:SOURCE_STRING_BATCH_01_MODE='scalar'
ctest --test-dir build/source-string-batch-ab-01-new/build -C Release -R "^(frontend_regressions|compiled_project)$" --output-on-failure
$env:SOURCE_STRING_BATCH_01_MODE='batch'
ctest --test-dir build/source-string-batch-ab-01-new/build -C Release -R "^(frontend_regressions|compiled_project)$" --output-on-failure
# Run the following through an unsandboxed shell/tool invocation:
python experiments/source-string-batch-ab-01/run.py build/source-string-batch-ab-01-new
```

prepare.py reuses the existing Fast Reader experiment's artifact-directory and
post-timing SHM digest shims, then restores file_context.cpp to the exact current
production reader. It introduces a private SOURCE_STRING_BATCH_01_MODE toggle
(scalar/batch); it does not alter production sources. CW_SOURCE_STRING_BATCH_V2
must be enabled in the generated build to provide its existing member buffer.

The native binary has no audit counters or failure hooks. The separate audit
target supplies Graph-operation counts and mid-commit/reserve injected failures.
Successful sorted diagnostics are printed only when the functional harness sets
SOURCE_STRING_BATCH_01_DIAGNOSTICS, after operation timing. Neither diagnostics
formatting nor SHM/file hashing is in native PUBLISH timing.

run.py requires the retained immutable fixture at
build/parallel-local-source-10-c9/ab-01/fixture. All modes use the same exact
project path, ordered inputs and a shared experimental artifact directory.
It performs two count/SHM prechecks, PUBLISH/audit/LOAD parity, warmup, 15
alternating native pairs, 7 separate phase-only pairs, then count/parity rechecks.
No samples are dropped. Do not run builds/tests concurrently with timing.
All executable, source and input hashes are checked again at the end.

Functional tests are self-contained and generated under the output directory.
They compare warnings/errors and full compiled/runtime/SHM output, including
repeated writes through references. Injected failures after a partial Graph
commit verify no public Project, persisted files or leaked SHM; they do not
require internal Graph/SourceMap transaction rollback or simulate every malloc.
