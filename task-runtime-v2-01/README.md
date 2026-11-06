# TASK-RUNTIME-V2-01

Opt-in SHM + Windows Events control transport for Task.exe. Existing V1 remains default. No change to Project SHM ABI, Graph, compiled.bin or scheduling.

## Apply

From repository root:

```powershell
python .\task-runtime-v2-01\apply.py .
git diff --check
cmake --build build/simtask-lib-01 --config Release --target ServerEngineV4 --parallel 8
```

Build Task.exe/fixture targets as well; host code is compiled into Task executable/library.

To test, set `TASK_RUNTIME_V2=1` for the server and run existing E2E-02. Run the same E2E without this variable for V1 parity. Do not switch production default until Windows tests pass.

## Limits

This phase replaces control transport only. `std::async`, DAG construction, and the 2ms server scheduler are unchanged. Named kernel objects use a per-launch unique token; handles are inherited by name, not by numeric value. Each child has its own control mapping and two auto-reset events. Process death and timeout are observed by the parent. This candidate is not yet validated on Windows. Project SHM attachment is transferred via a separate named mapping during startup.
