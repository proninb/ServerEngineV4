# TASK-RUNTIME-V2-PROBE-01

Isolated control-plane feasibility test, **not a ServerEngineV4 integration**.

Tests one named file mapping + two auto-reset Windows Events per child process.
Parent writes cycle to SHM, SetEvent(run), waits for all done events,
verifies sequence/status. Each child simulates zero-work TASKMAIN.

Build and run in **x64 Native Tools PowerShell for Visual Studio**:

```powershell
.\run.ps1 -Warmup 1000 -Samples 10000
```

Produces p50/p95/p99/max in ms for 1, 2, 4, 8, 16 Task.exe processes.

**Limitations:** This tests control-plane latency only, without Project SHM
attachment, TASKINIT/TASKDONE, DAG dependencies, failures, absolute tick scheduler,
or real workload. It does not prove a 1 ms deadline under Windows scheduling.
No production files, ABI, or git branches are modified. No commit or push.

**Architecture gate:** Compare this latency to the measured ~15.5 ms existing
IPC path on the same machine, then integrate V2 behind an opt-in build/runtime
switch, with lifecycle and failure parity tests before adoption.
