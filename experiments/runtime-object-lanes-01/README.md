# RUNTIME-OBJECT-LANES-01

Isolated Object worker-cap A/B experiment. Production defaults are unchanged. Results and decision: [report](../../docs/RUNTIME_OBJECT_LANES_01.md).

Requires current complete MSVC Release+IPO production benchmark objects in `build/publish-critical-path-02/symbols/ServerEngineV4PublishBenchmark.dir/Release` and the existing isolated UnitProXL fixture. Rebuild those objects if their source/configuration or ABI changes.

```powershell
cmake -S experiments/runtime-object-lanes-01 -B build/runtime-object-lanes-01 -A x64
cmake --build build/runtime-object-lanes-01 --config Release
ctest --test-dir build/runtime-object-lanes-01 -C Release --output-on-failure
python experiments/runtime-object-lanes-01/run.py build/runtime-object-lanes-01/audit-new --audit
python experiments/runtime-object-lanes-01/run.py build/runtime-object-lanes-01/results-new --pairs 20
```

Output directories must be new. The runner freezes the executable and source hashes, alternates same-executable pairs, verifies artifact goldens outside timing, and records coarse stage times. `--audit` separately verifies the full logical resident SHM; never mix audit times with performance measurements. `CW_RUNTIME_OBJECT_WORKERS` accepts 4/8/16/24 only in this experimental executable. Without the experimental macro, neither the cap nor the probe exists.

Measured decision: reject all three smaller caps; retain native 24-worker capacity on the measured machine.
