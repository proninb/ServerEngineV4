# RUNTIME-FIRST-TOUCH-01

Measured decision: reject. Full PUBLISH median paired slowdown 49.38 ms (20 pairs). See [report](../../docs/RUNTIME_FIRST_TOUCH_01.md).

Same-executable full PUBLISH A/B with 24 Object workers in both modes. Candidate adds a separate parallel page-touch pass over the Object tail immediately before Object initialization, after static-link marking. Each touched byte is read and written back unchanged; existing markers are preserved. No memset, large pages, or production enablement.

Uses the experimental `CW_RUNTIME_OBJECT_LANES_AB` harness and resident SHA auditor from runtime-object-lanes-01. `CW_RUNTIME_OBJECT_PRETOUCH=0/1` selects control/candidate. `pretouch_ms` includes boundary preparation and worker startup/join. `object_phase_ms` excludes pretouch; full PUBLISH and resident timers include it. The runner uses case 1 as a candidate label, not a one-worker configuration: actual Object lanes are asserted to be 24.

Requires complete current post-SHA production Release+IPO objects under `build/publish-critical-path-02/symbols/ServerEngineV4PublishBenchmark.dir/Release`, plus the existing isolated UnitProXL fixture. Rebuild production objects after source/configuration/ABI changes.

```powershell
cmake -S experiments/runtime-first-touch-01 -B build/runtime-first-touch-01 -A x64
cmake --build build/runtime-first-touch-01 --config Release
ctest --test-dir build/runtime-first-touch-01 -C Release --output-on-failure
python experiments/runtime-first-touch-01/run.py build/runtime-first-touch-01/audit-new --audit
python experiments/runtime-first-touch-01/run.py build/runtime-first-touch-01/results-new --pairs 20
```

Use new output directories. Audit hashes all logical resident bytes outside timing and peak capture. Performance series has no resident audit. Artifact goldens are checked outside the timer after every call. Two excluded warmup pairs precede 20 alternating measured pairs; no samples are filtered.
