# PUBLISH-CRITICAL-PATH-02

Completed capture: `build/publish-critical-path-02/capture-20261023-144348/`.
98 golden checks passed; control total median 1274.830 ms. Results and quality
limitations: `docs/PUBLISH_CRITICAL_PATH_02.md`. Kernel stacks dominate much of
apply_api_one's inclusive CPU; resolve them before choosing another executor
algorithm. Range memo and early context release remain off.

Refresh whole-PUBLISH profiling after accepted removal of acquisition SHA-256.
Uses a new complete production Release+IPO build, not objects from older benchmarks.
Header lazy state and member-name index 4x are enabled. Runtime range memo,
early context release, Source link-index 4x and String Table slot experiments are off.

## Prepare

```powershell
cmake -S . -B build/publish-critical-path-02/symbols -G "Visual Studio 18 2026" -A x64 -DSERVER_ENGINE_BUILD_BENCHMARKS=ON -DSERVER_ENGINE_ENABLE_IPO=ON -DSERVER_ENGINE_PUBLISH_NO_SHA256_AB=OFF -DSERVER_ENGINE_HEADER_STATE_LAZY_AB=OFF -DSERVER_ENGINE_CANONICALIZER_TRACE=OFF "-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /Zi" "-DCMAKE_EXE_LINKER_FLAGS_RELEASE=/DEBUG:FULL /OPT:REF /OPT:ICF /INCREMENTAL:NO"
cmake --build build/publish-critical-path-02/symbols --config Release --target ServerEngineV4PublishBenchmark -- /m:4
python experiments/publish-critical-path-02/freeze.py
```

Freeze verifies matching EXE/PDB GUID+age, production CMake flags, absence of audit/
A/B definitions and environment selectors. It records source hashes, Git HEAD,
CMake cache, Release definitions, EXE/PDB hashes and .text identity. Existing
`build/publish-critical-path-02/frozen/` cannot be overwritten.
Historical binaries and captures remain intact.

## Capture

Run in an elevated PowerShell (same privilege requirement as previous ETW captures):

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\Boris\ServerEngineV4\experiments\publish-critical-path-02\capture.ps1"
```

The launcher explicitly selects the new frozen executable and a new timestamped
directory under `build/publish-critical-path-02`. It cannot accidentally select the
pre-SHA `frozen-current-v2`. It runs the existing ownership-safe collector: two
excluded warmups, eight alternating control/sampled blocks, six processes per mode
and block (98 full PUBLISH calls). Golden hashes are checked externally after each
call. No global execution-policy change, self-elevation or cancellation of foreign
ETW sessions. Source remains enabled.

CSV now also includes lexical, Header physical, persistence, resident, compiled
encode/plan and transient-release intervals. Observer deltas are computed by paired
block means for every coarse timing field. Nested intervals must not be summed
as independent additions to full PUBLISH.

## Analyze after capture

```powershell
python experiments/source-cpu-sampling-02/export.py build/publish-critical-path-02/capture-TIMESTAMP build/source-cpu-sampling-02/reader/Reader.dll build/publish-critical-path-02/frozen
python experiments/publish-critical-path-02/analyze.py build/publish-critical-path-02/capture-TIMESTAMP
```

The first command reads real CPU SampledProfile events, verifies module/PDB identity
and trace loss, and exports stacks and existing Source group summaries. The second
adds `analysis/full-publish-summary.json`: all-function inclusive/exclusive CPU
counts, nearest distinct leaf callers, coarse phase attribution per block, control/
sampled wall statistics and observer deltas. Inclusive function counts overlap;
phase buckets partition target samples, including missing-stack and other buckets.
Deepest recognizable stage anchors are used; unknown worker/kernel stacks are not
assigned to a stage without evidence. Inline frames and ICF semantic names are not
invented. CPU sample counts are never treated as wall milliseconds.

Quality limits: audit stack coverage, lost buffers/events, actual sample interval,
module identity, unresolved application frames, block consistency and time drift.
Reader filters by recorded sampled block/PID pairs; it has no additional process-
lifetime filter. Strict zero-missing-stack quality is separate from useful broad
ranking; missing samples must remain visible in the report.

Select only one new algorithmic experiment after reviewing the new profile. Do not
reuse the former SHA hotspot or promote range memo/early release without new evidence.

## Preparation validation

The analysis tool was exercised against the historical October 23 **pre-SHA** trace:
359920 samples accounted exactly, including all 336 missing-stack samples. This is
a tool check and is not the new post-SHA CPU distribution. Existing tooling tests
passed 3/3. Native control preflight and golden results are recorded separately.

Native preflight: all ten full calls (two warmups, eight measured) preserved both
golden hashes; measured total median 1317.815 ms. This short control-only series
does not establish a new optimization gain. See `docs/PUBLISH_CRITICAL_PATH_02.md`.
