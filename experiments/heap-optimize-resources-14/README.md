# HeapOptimizeResources real-server diagnostic 14

User-authorized exception to the earlier no-trim diagnostic restriction. No
production adoption, new backing domain, allocator override or UNLOAD edit in the
working production tree. Only a separate frozen-source copy is patched.

## Protocol fixed before completion

- One Release + IPO, PGO OFF executable for both arms. Four-character environment
  selector SE_HEAP_OPT14=OFF_ / ON__; default is OFF.
- The only added call site follows successful UNLOAD's project.reset and precedes
  UNLOADED state publication. HeapSetInformation(nullptr, HeapOptimizeResources,
  initialized version/flags, sizeof(info)) is called only in ON.
- Both arms record identical telemetry: API interval QPC, BOOL/error, PrivateUsage
  before/after, selector and measurement validity. Log writes occur after the
  before/after interval, not inside it. OFF success=false means not called, not an
  API failure; called is stored separately.
- Pilot: four fixtures; same logged-in TCP across PUBLISH/RUN/FREEZE/UNLOAD/LOAD/
  UNLOAD, artifact/native/value parity and API/log validity.
- Sequential **600-second measured OFF then 600-second measured ON**, ten warmup
  cycles each, from archived TCP-PGO-RELEASE-02 soak. Same fixture order, periodic
  UnitProXL, intentional connection-error/reconnect pattern and external quiescent
  checkpoint. Healthy TCP stays the same through both UNLOADs before intentional
  reconnect. External TCP UNLOAD latency is recorded separately.
- Unchanged gate: each fixture's last-five minus first-five measured quiescent
  median <=128 MiB Private Bytes drift and <=16 handles. Not a peak/working-set
  substitute. A completed OFF memory-gate failure is expected evidence and does
  not prevent ON; correctness/incomplete-run failure prevents continuation.
- Preliminary candidate evidence requires OFF reproduces memory failure, ON
  passes the same gate, all correctness checks complete and all ON API calls
  succeed. If OFF does not reproduce, do not claim the original failure fixed.
- Report API/UNLOAD latency distributions regardless of memory outcome. One
  sequential pair is not production certification or a general physical-release
  guarantee. _heapmin, HeapCompact and EmptyWorkingSet are not used.

Scripts: prepare.py, heap_optimize_diagnostic.hpp, prepare_harness.py,
run_soak.py, pilot.py, driver.py and analyze.py. Run prepare/build/pilot first;
driver.py executes both long arms, analyze.py accepts only completed runs.
All output directories and final analysis files are new; frozen evidence is not
overwritten. See build/heap-optimize-resources-14/source-delta.json and
harness-origin.json for exact source/harness scope.

Microsoft: [HeapSetInformation](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapsetinformation)
and [HEAP_OPTIMIZE_RESOURCES_INFORMATION](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-heap_optimize_resources_information).
Success does not promise memory is decommitted; Windows performs that if possible.
