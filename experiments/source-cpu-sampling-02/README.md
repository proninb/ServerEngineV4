# SOURCE-CPU-SAMPLING-02

## Refresh after accepted V2 optimizations (2026-10-23)

Later update: `PUBLISH_NO_SHA256_01.md` accepts removal of acquisition digests.
The existing `frozen-current-v2` and its October 23 trace predate that change and
must stay immutable. Before profiling the latest production code, prepare a new
Release+IPO EXE/PDB in a new frozen directory and select it explicitly with
`capture.ps1 -Executable <new-ServerEngineV4PublishBenchmark.exe>`. Do not interpret
the launcher's historical default as the latest source revision.

The default `capture.ps1` now uses `build/source-cpu-sampling-02/frozen-current-v2/`.
This is a separate Release + IPO + /Zi build with lazy Header state and 4x Graph
member-name index growth. A/B controls and diagnostic counters are disabled.
The original frozen pair and October 22 captures remain available for historical analysis.

Build using the command below with `symbols` replaced by `current-v2`, adding
`-DSERVER_ENGINE_HEADER_STATE_LAZY_AB=OFF`, then run `python
experiments/source-cpu-sampling-02/freeze_current.py` once. The snapshot records
source hashes, Git HEAD, CMake cache and matching EXE/PDB identities; the destination
cannot be overwritten. Do not rebuild or replace the frozen pair during capture.

From an elevated PowerShell, avoiding the previously encountered script-policy error:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\Boris\ServerEngineV4\experiments\source-cpu-sampling-02\capture.ps1"
```

The refreshed CSV includes `header_ms`; paired observer overhead is reported for
Header, Source and total PUBLISH. It still measures full PUBLISH with Sources enabled.
`-Executable <absolute-path>` explicitly selects another matching EXE/PDB pair.
After capture, use `export.py <capture-directory> <Reader.dll> <frozen-current-v2-directory>`.
Only interpret current hotspots after auditing stacks and loss; IPO/ICF symbol names
alone do not establish which semantic operation executed.

Preparation validation: the fresh build and EXE/PDB identity check passed. In
`build/source-cpu-sampling-02/current-v2-control-20261023/`, two warmups and two
measured full PUBLISH calls all preserved both golden hashes. Measured totals were
1330.63 and 1325.13 ms; this short smoke check is not a new speedup claim. Tool tests
passed 3/3. Actual refreshed CPU samples still require the elevated capture above.

Production Source V2 remains unchanged. No clock probes, local WHO database,
dispatcher/window, alternate parser or Graph layer. This folder prepares ETW CPU
sampling with stacks, controls its whole-PUBLISH overhead and verifies symbols.

## Optimized symbols build

The preserved production benchmark has no Release PDB/RSDS debug identity. Keep it
untouched. Build an additional **production-code** executable with /Zi and matching
PDB, retaining Release /O2 /Ob2 /DNDEBUG, IPO/LTCG, /OPT:REF and /OPT:ICF:

```powershell
cmake -S . -B build/source-cpu-sampling-02/symbols -G "Visual Studio 18 2026" -A x64 -DSERVER_ENGINE_BUILD_BENCHMARKS=ON -DSERVER_ENGINE_ENABLE_IPO=ON "-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /Zi" "-DCMAKE_EXE_LINKER_FLAGS_RELEASE=/DEBUG:FULL /OPT:REF /OPT:ICF /INCREMENTAL:NO"
cmake --build build/source-cpu-sampling-02/symbols --config Release --target ServerEngineV4PublishBenchmark -- /m:4
python experiments/source-cpu-sampling-02/collect.py --exe build/source-cpu-sampling-02/symbols/Release/ServerEngineV4PublishBenchmark.exe --project build/publish-subsecond-01/fixture/project.json --out build/source-cpu-sampling-02/preflight --check
python experiments/source-cpu-sampling-02/test_tools.py
```

`symbols.py` reads the EXE CodeView RSDS GUID+age and the actual MSF7 PDB info stream.
It rejects a mismatching PDB, rather than trusting filenames/timestamps. SHA256 and
PE .text SHA256 are recorded. The new .text hash differs from the old no-PDB binary:
it is not claimed to be the old executable with a retrofitted matching PDB. All
future control and sampled runs must use this **same new EXE/PDB**, without rebuilding.
Source code/algorithms and optimization settings remain production; no probes are added.

## Capture (requires elevated Windows token)

From an administrative PowerShell terminal in this workspace:

```powershell
& .\experiments\source-cpu-sampling-02\capture.ps1
```

The launcher chooses a new timestamped results directory. It does not self-elevate,
change security policy or disable paging/IPO. Existing datasets are not overwritten.
The collector requires a project under build/, uses the UnitProXL fixture, verifies
golden compiled/runtime SHA256 after every call and records every benchmark PID.

Two excluded warmups, then eight control/sampled blocks in alternating order, six
fresh-process full PUBLISH calls per mode/block (98 calls including warmup). Both
modes use the same publish-timeline interface with old per-token/member clocks off.
PUBLISH total, Source wall, caller timestamps and PIDs are saved in measurements.csv.
Each sampled block owns one `wpr -start CPU -filemode` session with a unique instance
name; its corresponding `wpr -stop` always uses that name. The script never cancels
or stops another/default session. Start failure does not trigger a foreign stop.
If stop fails, the owned instance name is saved for explicit recovery.

WPR's built-in CPU.Verbose.File profile requests CPU sampling stacks. Sampling
interval is not changed globally. Save profile/session configuration and audit actual
sample intervals, stacks and dropped events in the resulting ETL before analysis.
Golden hashing and Python are outside benchmark-reported total, though inside the
recording; exclude them by the recorded process IDs. Control-only validation:

```powershell
python experiments/source-cpu-sampling-02/collect.py --exe build/source-cpu-sampling-02/symbols/Release/ServerEngineV4PublishBenchmark.exe --project build/publish-subsecond-01/fixture/project.json --out build/source-cpu-sampling-02/another-control --blocks 4 --repeats 4 --control
```

No builds, tests or other profilers should run during measurement. Compare per-block
means for ETW-minus-control; do not compare a profiled median with an older baseline.
The collector finishes with `captured_not_analyzed`, never with a successful CPU
ranking merely because a nonempty ETL exists.

## Stack analysis and quality gates

Open the ETL in WPA, set local symbols to the frozen EXE/PDB directory, and load the
CPU Usage (Sampled) table. Audit symbol GUID matches, stack presence, lost events,
sample interval/weight and process identities. Filter only the sampled block/PID
pairs from measurements.csv. Keep CPU samples/weights distinct from wall-clock time.
Use inclusive and exclusive function views; inspect inline attribution and missing
frames under IPO. A helper absent from stacks is not evidence of zero CPU cost.

`analyze.py` consumes **normalized complete-stack rows**, not WPA tree subtotals:

```json
{"block":0,"pid":1234,"frames":["module!caller","module!callee"],"count":20,"weight_ms":20.0}
```

`frames` are root-to-leaf; `weight_ms` is the total row weight, not weight per sample.
Normalize actual WPA/exported stacks explicitly. An automatic WPA-tree CSV-to-JSON
adapter has not been validated without a real ETL; do not silently reinterpret a
tree/subtotal export as independent complete stacks.

```powershell
python experiments/source-cpu-sampling-02/analyze.py samples.jsonl measurements.csv analysis.json --quality quality.json
```

The quality file must contain audited `lost_events`, `lost_stacks`, `symbol_audited`.
No audit or insufficient data prevents acceptance. Preliminary thresholds: at least
10,000 recognizable Source CPU samples across at least four blocks, zero measured
event/stack loss, audited symbols, no unresolved application samples. These are
minimum review gates, not a statistical proof of representativeness or independence.

Groups cover cursor, binding, object lookup, endpoint/member transitions, path
interning, Graph writes, provenance and other functions. Inclusive groups overlap;
each Source sample contributes once to its actual exclusive leaf category. Recursion
is deduplicated for inclusive function counts. IPO-eliminated frames are not invented.
Samples without recognizable Source frames remain unclassified by phase; do not
force every Graph function in the entire PUBLISH into Source. Native allocation/
memcpy CPU remains at the real leaf in exclusive reports, with its parents inclusive.

## Current environment limitation

WPR start returned `0xc5585011` (failed to enable profile-system-performance policy).
The current token has no SeSystemProfilePrivilege. The alternate xperf attempt
returned 0x3ec (Invalid flags); this does not by itself prove a privilege cause.
WPR reports no active recording afterwards. No valid CPU ETL/sample distribution
was collected. Scripts/symbols/accounting can be validated here, but actual ETW
capture, loss auditing and WPA export require the administrative run above.


## Actual capture analysis

The administrative capture succeeded in capture-20261022-212914. Read the final
results and stack-coverage limitations in docs/SOURCE_CPU_SAMPLING_02.md. The earlier
privilege limitation above describes the initial non-elevated token only.

The real ETW adapter is reader/Reader.csproj (net10.0). It uses the installed
Visual Studio TraceEvent/FastSerialization and NETCore.Client assemblies; no NuGet
package download is needed. Build with BaseIntermediateOutputPath and output under
build/source-cpu-sampling-02, UseSharedCompilation=false. Copy the installed DIA SDK
amd64/msdia140.dll beside Reader.dll (also under its amd64/ subdirectory).

```powershell
dotnet build experiments/source-cpu-sampling-02/reader/Reader.csproj -c Release -o build/source-cpu-sampling-02/reader -p:BaseIntermediateOutputPath=C:/Boris/ServerEngineV4/build/source-cpu-sampling-02/reader-obj/ -p:UseSharedCompilation=false
python experiments/source-cpu-sampling-02/export.py build/source-cpu-sampling-02/capture-20261022-212914 build/source-cpu-sampling-02/reader/Reader.dll build/source-cpu-sampling-02/frozen
```

export.py validates frozen EXE/PDB identity, converts actual SampledProfile events,
checks xperf losses and writes full-stack JSONL plus inclusive/exclusive summaries.
It does not run PUBLISH. DIA symbol resolution may require execution outside the
sandbox. Generic analyze.py retains its stricter zero-missing-stack acceptance gate;
the actual export explicitly reports coverage limitations, rather than silently
relaxing that gate. All ETL/ETLX/JSONL and native binaries remain under ignored build/.
