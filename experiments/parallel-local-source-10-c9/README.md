# PLS10-C9: comparative cost attribution

No algorithm changes. C7 remains experimental; production stays V2.

`run.py <new build directory>` freezes the original C7 executable (both V2/C7
dispatches) and the coarse executable. It runs 15 alternating uninstrumented
pairs, then seven blocks comparing original, regenerated observer-off, and
observer-on using `publish-timeline` (phase-only telemetry). Hashing is outside
the measured child; both semantic domains have independent artifact baselines.
Cold checks verify the full native object-storage digest for each arm.

Build with the same MSVC Release/IPO compiler and core as C7:

```powershell
cmake -S experiments/parallel-local-source-10-c9 -B build/parallel-local-source-10-c9 -G "Visual Studio 18 2026" -A x64 -DPLS10_CORE_LIBRARY=C:/Boris/ServerEngineV4/build/parallel-local-source-10-c4/base/Release/PLS08Core.lib
cmake --build build/parallel-local-source-10-c9 --config Release --target PublishCost10C9
python experiments/parallel-local-source-10-c9/run.py build/parallel-local-source-10-c9/ab-01
```

Do not build or run other tests during timed blocks. The runner never enables
C8. Its frozen original executable supplies the performance comparison;
observer-off measures regeneration/code-layout effects separately.

Coarse scope granularity is one file, not one operation. C7 reports extraction
worker wall/CPU/cycles sums, main-thread wait, apply and destruction. V2 reports
fused per-file syntax/semantic/Graph work. Source process CPU includes workers;
Source thread CPU measures the commit thread. These are overlapping scopes and
must not be summed as a wall-time partition. Windows thread CPU has coarse
accounting resolution; zero does not imply zero work. Cycle counts are work
indicators, not cache misses or convertible wall-time estimates.

Model capacity is C7 vector payload capacity, not allocations or RSS. It includes
models between construction and main-thread destruction; workers' in-progress
construction capacity is excluded from this particular counter. Source tokens
are shared acquisition input, not another local-model copy. WS begin/end are
snapshots; the benchmark's peak WS is process-lifetime peak, not a Source-only
high-water mark.

`SourceAudit10C9` is a separate explicit target. It links count-only generated
Graph/identity copies and replacement C++ new/delete. Counts are scoped to Source
on the main and extraction threads. Live payload follows allocation ownership
across threads and later frees. It excludes malloc/HeapAlloc/mapped memory and
allocations made before Source (including acquisition and Header); it is not a
total-process allocator census. Its timing must not rank performance. No
per-operation timer is used in either executable. Hardware cache locality is
not measured by these tools.

Audit allocation groups partition calls/requested bytes: 1 = other Source work
(all fused V2 work), 2 = C7 extraction, 3 = C7 apply_file. The sum is checked by
`audit.py`. Build the explicit audit target, then run:

```powershell
cmake --build build/parallel-local-source-10-c9 --config Release --target SourceAudit10C9
python experiments/parallel-local-source-10-c9/audit.py build/parallel-local-source-10-c9/ab-01
python experiments/parallel-local-source-10-c7/test.py build/parallel-local-source-10-c9/Release/PublishCost10C9.exe build/parallel-local-source-10-c9/Release/SourceAudit10C9.exe build/parallel-local-source-10-c9/fixtures
```
