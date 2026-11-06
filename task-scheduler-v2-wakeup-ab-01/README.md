# TASK-SCHEDULER-V2-WAKEUP-AB-02

Read-only benchmark runner. Run from `C:\Boris\ServerEngineV4`.

1. Extract this ZIP to the repository root. It creates `task-scheduler-v2-wakeup-ab-01/run_ab.py`.
2. Ensure previously supplied `task-runtime-v2-performance-01`, `task-performance-01`, and `task-server-e2e-01` folders exist. The script checks these before starting.
3. Execute:

```powershell
cd C:\Boris\ServerEngineV4
python -u .\task-scheduler-v2-wakeup-ab-01\run_ab.py `
  --server .\build\simtask-lib-01\Release\ServerEngineV4.exe `
  --config .\build\tcp-pgo-release-02\tcp-integration\candidate\server.json `
  --project .\build\tcp-pgo-release-02\fixtures\unit\project.json `
  --fixture-dir .\build\simtask-lib-01\Release `
  --duration 8 --warmup 2 --orders AB BA
if ($LASTEXITCODE -ne 0) { throw "WAKEUP AB/BA failed: $LASTEXITCODE" }
```

A uses `TASK_SCHEDULER_WAKEUP=0`; B uses `=1`. Both use `TASK_RUNTIME_V2=1`, `TASK_SCHEDULER_V2=1`, and `TASK_SCHEDULER_DEADLINE=1`. Each arm is a fresh server process. All results live under a timestamped `build/task-scheduler-v2-wakeup-ab-02` directory; each arm has `run.log`, `deadline.csv`, `summary.csv`, and benchmark CSVs. Top-level `results.csv` is updated after each successful arm.

This measures cadence and deadlines, **not CPU usage**. CPU overhead and independent verification of V2 IPC must be checked before adoption. Do not infer hard real-time 2ms guarantees from any successful benchmark. No code, Git or production settings are changed.
