# SOURCE-V2-CRITICAL-01

Current production V2 + Fast Reader + existing Batch ON. Actual unsandboxed ETW
CPU sampling, matching optimized PDB, kernel MM separation, isolated Graph link
replay and one tested duplicate-lookup candidate. **NO ADOPTION:** native paired
gain 11.51 ms, CI −8.18–24.44 ms. Production unchanged.
Report: `docs/SOURCE_V2_CRITICAL_01.md`.

Use a new output directory. Snapshot/build:

```powershell
python experiments/source-v2-critical-01/prepare.py build/source-v2-critical-01-new
cmake -S build/source-v2-critical-01-new/source -B build/source-v2-critical-01-new/build -G "Visual Studio 18 2026" -A x64 -DBUILD_TESTING=OFF -DSERVER_ENGINE_BUILD_BENCHMARKS=ON -DSERVER_ENGINE_ENABLE_IPO=ON -DSERVER_ENGINE_PUBLISH_PGO=OFF -DSERVER_ENGINE_PRODUCTION_PGO=OFF "-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /Zi /Zo /MP4" "-DCMAKE_EXE_LINKER_FLAGS_RELEASE=/DEBUG:FULL /OPT:REF /OPT:ICF /INCREMENTAL:NO"
cmake --build build/source-v2-critical-01-new/build --config Release --target ServerEngineV4PublishBenchmark --parallel 2
```

Run the build outside sandbox if PDB manager reports C1902. Before capture,
launch recorder.ps1 with -Directory pointing at the snapshot's capture directory
from an elevated PowerShell/UAC helper. Run collect.py with the output directory
as its argument from an **ordinary unsandboxed** process. The helper only handles
WPR; it does not launch/elevate benchmark children. Confirm recorder-ready.json
before collection. Requests are bounded to start/stop/finish, output is restricted
to the workspace build folder, and only the helper's own instance is stopped.
The helper has a 20-minute safety deadline; collect.py must finish within it.

collect.py uses the exact retained original C9 fixture and known compiled/runtime
hashes. It freezes matching EXE/PDB, runs 4×8 sampled/control pairs in alternating
block order plus 2 warmups, and verifies source/input/binary identities. Preserve
all logs/ETLs and never overwrite a frozen directory. Do not build/analyze or run
other benchmarks during capture. Recording is not performance timing evidence
without the corresponding controls and observer-cost report.

Reader: build reader/Reader.csproj into build/<output>/reader, with
BaseIntermediateOutputPath under that build directory. It uses installed
Microsoft TraceEvent assemblies, no new package. Copy installed VS DIA SDK
bin/amd64/msdia140.dll into reader/amd64 and reader for native symbol resolution.
The reader initially works with the local cache. fetch_symbols.py downloads
matching public PDBs from Microsoft's official symbol server into system-symbols
under the chosen output directory, validating GUID and DBI age. Re-run export
with the reader's local system-symbol path enabled to resolve kernel/MM frames.

Use the existing `experiments/source-cpu-sampling-02/export.py` with capture dir,
reader/Reader.dll, and frozen EXE/PDB dir. It audits xperf losses and emits real
SampledProfile stacks only. No interpretation of context-switch events as CPU.
Application GUID/age mismatches are fatal. Some system symbols can be unavailable;
retain and disclose the unresolved counts. analyze.py reports inclusive overlaps,
actual callers of ICF-folded names and the Source-only CPU sample weights.

index-replay/CMakeLists.txt links the same optimized production .obj. Its default
snapshot path is this experiment's original build directory; adjust SNAPSHOT in
a separate copy for another frozen run. Replay main accepts the original fixture's
compiled.bin and verifies all 367144 link targets and returned slots each round.
It is a diagnostic component replay, not a replacement PUBLISH benchmark.

Only after profiling/replay, prepare_candidate.py writes an isolated Graph.cpp
variant and benchmark into candidate-source and records their hashes. Configure
and build that folder into candidate-build. It links all original production
objects except Graph and benchmark main. Test with test_candidate.py plus
SourceCriticalGraphTests in baseline/candidate modes. run_candidate.py performs
15 native and 7 separate coarse-profile pairs, outside sandbox with ETW stopped;
it freezes the executable, verifies all artifacts and SHM, and restores original
fixture artifacts. Environment toggle SOURCE_CRITICAL_LINK_MODE=candidate
exists only in this diagnostic build. The original append tail is copied intact.

The candidate's acceptance rule is full native PUBLISH CI excluding zero with
parity. The current run fails that rule; the production files were never patched.
