# TASK-SERVER-E2E-02

Isolated, real ServerEngineV4 TCP and Task.exe execution test. Uses existing compiled.bin as LOAD input; does not PUBLISH or BUILD. Creates a temporary Project, unique SHM name, random TCP port, and test-only callback executables. No production source changes. The original fixture remains unchanged.

1. Extract this directory beside `task-server-e2e-01` under repository root.
2. `python .\task-server-e2e-02\apply.py .`
3. `cmake -S . -B build/simtask-lib-01 -A x64 -DSERVER_ENGINE_ENABLE_IPO=OFF -DSERVER_ENGINE_PRODUCTION_PGO=OFF`
4. `cmake --build build/simtask-lib-01 --config Release --target ServerEngineV4 task_server_e2e_fixture_0 task_server_e2e_fixture_1 task_server_e2e_fixture_2 --parallel 8`
5. Run `python .\task-server-e2e-02\task_server_e2e_02.py --server .\build\simtask-lib-01\Release\ServerEngineV4.exe --config .\build\tcp-pgo-release-02\tcp-integration\candidate\server.json --project .\build\tcp-pgo-release-02\fixtures\unit\project.json --fixture-dir .\build\simtask-lib-01\Release`

Test asserts callbacks, SHM attachment presence, dependency ordering, FREEZE skipping normal tasks, UNLOAD DONE, repeat LOAD and TASK_FAIL -> FREEZE. It does not prove SHM unmapping or OS handle reclamation; those need dedicated resource probes. Windows validation pending.
