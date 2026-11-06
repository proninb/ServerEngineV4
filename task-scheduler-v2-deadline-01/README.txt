TASK-SCHEDULER-V2-DEADLINE-01

Local source patch for server_engine/server.cpp from uploaded task-deadline-source.zip.
Apply from repository root:
  python .\task-scheduler-v2-deadline-01\apply.py --root .

Build the same local configuration you used for TASK-SCHEDULER-V2-01:
  cmake --build .\build\simtask-lib-01 --config Release --parallel 8
  ctest --test-dir .\build\simtask-lib-01 -C Release --output-on-failure

Enable only for diagnostic runs:
  $env:TASK_RUNTIME_V2='1'
  $env:TASK_SCHEDULER_V2='1'
  $env:TASK_SCHEDULER_DEADLINE='1'
  $env:TASK_SCHEDULER_DEADLINE_CSV='C:\Boris\ServerEngineV4\build\task-deadline-8.csv'
Run your existing task-runtime benchmark or server executable, then:
  python .\task-scheduler-v2-deadline-01\analyze.py .\build\task-deadline-8.csv

Important:
- CSV file opened in 'w' mode for each server process; for multi-run benchmark use a unique path per run.
- No instrumentation when TASK_SCHEDULER_DEADLINE is not exactly '1'.
- Diagnostic CSV writes add overhead; compare with an instrumented baseline.
- deadline_miss means completion later than scheduled start + 2ms; it is not a formal realtime guarantee.
- skipped_periods counts periods skipped after completed work in absolute-cadence mode.
- No commits/pushes. Backup server_engine/server.cpp.deadline01.bak created before patch.
