# TASK-RUNTIME-V2-PERFORMANCE-01 — candidate, not validated on Windows

Full ServerEngineV4 TCP LOAD/RUN/FREEZE/UNLOAD/SHUTDOWN benchmark, 2 ms configured scheduler, 1/2/4/8 Task processes, V1 vs opt-in V2. Uses the already established `task-performance-01` and `task-server-e2e-01` test fixtures and project. No PUBLISH/BUILD/compiled.bin changes. The patch is diagnostic-only and is not committed.

1. Extract `task-runtime-v2-performance-01` to the repository root, beside `task-performance-01` and `task-server-e2e-01`.
2. **Make a git diff backup first.** The patch modifies server.cpp, task_process.cpp, task_manager.cpp and adds task_perf_probe.hpp. It expects the current V2-01 source, and refuses to apply when the header already exists.
3. `python .\task-runtime-v2-performance-01\apply.py .` (once only)
4. `cmake --build .\build\simtask-lib-01 --config Release --parallel 8`
5. `ctest --test-dir .\build\simtask-lib-01 -C Release --output-on-failure`
6. Run benchmark (replace paths with your previously working TASK-PERFORMANCE-01 paths):

```powershell
python .\task-runtime-v2-performance-01\run.py `
  --server .\build\simtask-lib-01\Release\ServerEngineV4.exe `
  --config .\build\tcp-pgo-release-02\tcp-integration\candidate\server.json `
  --project .\build\tcp-pgo-release-02\fixtures\unit\project.json `
  --fixture-dir .\build\simtask-lib-01\Release `
  --counts 1 2 4 8 --duration 8 --warmup 2
```

Result: `build/task-runtime-v2-performance-01/summary.csv` plus per-mode throughput and stage CSV files. V1 and V2 each run in a separate server process, with TASK_RUNTIME_V2 explicitly 0 or 1. `TASK_PERF02_CSV` records stages on process exit; stage samples include warmup. `ipc_roundtrip` measures RUN exchange in both modes. `barrier_wait` is the time spent joining already dispatched futures, **not** the full barrier. `cycle_interval` p50/p95/p99 is not proof of 2 ms deterministic scheduling. The benchmark does not independently prove that the V2 code path was selected; inspect server child command line for `--serverengine-task-v2` or add a dedicated mode assertion before declaring V2 E2E PASS. The script measures but does not change scheduler architecture, precompute DAG, or remove std::async.

No commit/push. V1 remains default. Windows build and performance results must be supplied by the user.
