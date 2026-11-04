# TASK-SERVER-E2E-01 — real TCP lifecycle smoke test

Requires an existing valid Server `server.json` (TCP listener, license, authentication) and Project `project.json` with compiled.bin and Task configuration. No source changes or publish. Starts its own ServerEngineV4 process. **Do not run against an occupied port or live server.**

```powershell
python .\task_server_e2e.py --server C:\Boris\ServerEngineV4\build\simtask-lib-01\Release\ServerEngineV4.exe --config C:\PATH\server.json --project C:\PATH\project.json --port 12345
```

Change port to the actual configured TCP port. Checks LOGIN, LOAD, RUN, FREEZE, RUN, FREEZE, UNLOAD, second LOAD without restarting server, and SHUTDOWN with correlated JSON request IDs. Each state checked through GET_STATE on the same TCP connection. Prints server log path.

**Limitations:** This is a server lifecycle TCP smoke test, not proof of TASKINIT/TASKMAIN/TASKDONE execution, child-process cleanup, SHM unmapping, dependency order, task failure or runtime counters. Those require observable fixture instrumentation and a controlled Project; do not mark full Task Server E2E PASS on this test alone. No CMake/production changes, no commit/push.
