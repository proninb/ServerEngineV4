# TCP-RELIABILITY-01

See docs/TCP_RELIABILITY_01.md for the explicit name-only trusted-LAN model, tested failure paths and limits. Fault hooks are PRIVATE to the communication regression target; production has no fault environment controls.

From repository root on Windows/MSVC:

```powershell
cmake -S . -B build/tcp-reliability-01 -G "Visual Studio 18 2026" -A x64 -DBUILD_TESTING=ON -DSERVER_ENGINE_PRODUCTION_PGO=OFF
cmake --build build/tcp-reliability-01 --config Release --target ServerEngineV4 ServerEngineV4CommunicationRegressionTests ServerEngineV4CommunicationConnectionCoreTests ServerEngineV4CommunicationControlTests ServerEngineV4CommunicationProtocolTests ServerEngineV4ServerPolicyTests ServerEngineV4ClientSessionTests
ctest --test-dir build/tcp-reliability-01 -C Release -R "^(communication_|client_session$|server_policy$)" --output-on-failure
```

Run real TCP integration unsandboxed, with existing local license/lease and fixtures. The output directory must not already exist:

```powershell
python experiments/tcp-reliability-01/integration.py build/tcp-reliability-01/integration-new --candidate build/tcp-reliability-01/Release/ServerEngineV4.exe --reference build/production-pgo-current-02/ab-01/binaries/pgo/ServerEngineV4.exe
```

Dependencies: the existing production-pgo-01 TCP client and read-only native SHM auditor, frozen UnitProXL at build/production-pgo-current-02/fixtures/unit, measured reference executable. The harness creates its own small fixture and session directories, generates artifacts, and terminates only its own processes on test failure. It does not train PGO, deploy, or time performance. It verifies binary hashes remain unchanged. Snapshot results in results/20261027 are correctness evidence, not a self-contained fixture/license/binary distribution.
