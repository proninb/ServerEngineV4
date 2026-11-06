# TASK-SCHEDULER-V2-01

Opt-in Windows timer resolution (1 ms request) and absolute steady-clock tick cadence for the existing ServerEngineV4 control loop. Requires existing `server.cpp` with the original 2 ms deadline and tick advance anchors; compatible with task performance instrumentation if those anchors remain unchanged.

## Apply

From repository root:

```powershell
python .\task-scheduler-v2-01\apply.py .
if ($LASTEXITCODE -ne 0) { throw 'Scheduler patch failed' }
cmake --build .\build\simtask-lib-01 --config Release --parallel 8
ctest --test-dir .\build\simtask-lib-01 -C Release --output-on-failure
```

## Benchmark

Set `TASK_RUNTIME_V2=1` and `TASK_SCHEDULER_V2=1` for the candidate server process. For a controlled A/B run, the runner must propagate `TASK_SCHEDULER_V2` to the child server. Existing `run.py` may override the environment; inspect it before running. Use `--modes v2` and repeat with scheduler OFF/ON. Compare cycle interval p50/p95/p99 and barrier wait. Expected precision is not guaranteed: Windows scheduler and queue condition-variable implementation can limit results.

## Contracts

- Default OFF; no change to V1 or V2 IPC.
- One outstanding tick; no overlap of Tasks.
- Skip missed deadlines rather than execute catch-up bursts.
- Timer resolution is restored when server run() exits.
- Does not change Project lifecycle or SHM ABI.
- No commit or push.

## Caveats

This is a candidate, not tested on Windows. `timeBeginPeriod(1)` is a request, not a real-time guarantee. The server request queue uses system_clock deadlines, so wall-clock adjustments can affect individual waits. On Windows 11, timer behavior can also depend on process visibility/power policy. If the measured cycle remains ~15.6 ms, inspect the queue wait implementation and consider a dedicated high-resolution waitable timer plus queue wake event in a subsequent architecture step.
