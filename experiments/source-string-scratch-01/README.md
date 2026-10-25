# SOURCE-STRING-SCRATCH-01

Result: no production adoption. Source paired median gain 7.5865 ms; full-PUBLISH paired median gain 0.680 ms, 10/20 faster, confidence interval includes zero. [Report](../../docs/SOURCE_STRING_SCRATCH_01.md).

One Source V2 change: reuse the capacity of decoded string bytes and temporary endpoint-path steps between `assign_string` calls. Both buffers are cleared before use. G receives values and canonical endpoint handles, never references into these buffers. No change to string-path cache, string batching, WHO, overwrite diagnostics, or SourceMap.

Candidate is experiment-only (`CW_SOURCE_STRING_SCRATCH_AB`). `CW_SOURCE_STRING_SCRATCH=0/1` selects local/reference or parser-owned/reuse buffers in the same executable. Normal production keeps the original local buffers until a separate acceptance decision.

Build requires complete current post-SHA Release+IPO objects from `build/publish-critical-path-02/symbols/ServerEngineV4PublishBenchmark.dir/Release`. Rebuild that baseline after source/configuration or ABI changes. Benchmark and Source Parser are rebuilt for this experiment.

```powershell
cmake -S experiments/source-string-scratch-01 -B build/source-string-scratch-01 -A x64
cmake --build build/source-string-scratch-01 --config Release
ctest --test-dir build/source-string-scratch-01 -C Release --output-on-failure
python experiments/source-string-scratch-01/run.py build/source-string-scratch-01/audit-new --audit
python experiments/source-string-scratch-01/run.py build/source-string-scratch-01/results-new --pairs 20
```

Use fresh output directories. Audit uses `SourceStringScratch01Audit`, with capacity-growth counters only around these two real vectors. A successful capacity change counts one backing-buffer allocation and requests `capacity * sizeof(value_type)` bytes under the installed MSVC STL. Counts exclude allocator bookkeeping, frees, other parser/Graph allocations, and failed allocations. Diagnostic timings are not used to select the optimization.

Timing uses `SourceStringScratch01` with no growth counters, two excluded warmup pairs, and 20 alternating measured pairs. Labels `workers=0/1` in the generic runner CSV mean scratch control/reuse, not worker counts. Both modes retain the same scheduling. SHA-256 artifact verification is outside the measured lifecycle after every call. No SHM pretouch or Object worker experiment macro is enabled. The initial audit directory from 2026-10-23 uses legacy generic control label 24 for the same local-buffer mode; `string_scratch_reuse` is the authoritative selector.

Tests run the full frontend regression suite and 22 Header V2 semantic fixtures in both modes. Additional string tests cover long-to-short/empty writes across different path depths and ensure G retains final values after scratch mutation/destruction. Existing tests cover adjacent literals, escapes, bounds errors, inherited members, and repeated-write warnings.
