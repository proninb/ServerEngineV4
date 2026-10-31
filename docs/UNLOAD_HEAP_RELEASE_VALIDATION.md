# Integrated UNLOAD heap candidate — release validation

2026-10-30. PRELIMINARY VALIDATION PASS ON TESTED WINDOWS 11 X64.
NO ADOPTION. Default OFF / PGO OFF / release BLOCKED pending explicit adoption
and review of the bounded validation scope. No deployment, commit or push.

## Frozen candidate and agreed scope

Exact existing ON image, no rebuild:
`build/heap-integration-candidate/on/Release/ServerEngineV4.exe`

SHA-256: `a4c3e005998e463b66aefd4e9c96ac6e67274f14657a46e293687af51a4282d9`.

User selected Windows 11 x64 as the mandatory OS family. Actual host: Microsoft
Windows 11 Home 10.0.26200 x64. Executed configuration: windows-x64 ABI pack8,
fixed_direct SHM, localhost TCP, name-only LOGIN. This is evidence for this host
and configuration, not a claim that every Windows 11 edition/build or ABI/SHM
configuration was exercised. Older OS execution was not required by the selected
matrix and is not claimed; compatibility fallback remains documented separately.

## Unmodified-process 600-second soak

Ten warmup cycles followed by 601.168 measured seconds. 991 total cycles,
981 measured. Existing fixture schedule and quiescent gate unchanged: per-fixture
last-five minus first-five measured medians <=128 MiB PrivateUsage drift and
<=16 handles drift. No forced heap trim, restart, altered threshold or debugger
attach in the soak process. Maximum adjacent measured checkpoint interval 6.153 s.

| Fixture | Private drift MiB | Handle drift | Original gate |
|---|---:|---:|---|
| UnitProXL | -5.58 | 0 | PASS |
| strings | +9.91 | 0 | PASS |
| inheritance | +10.02 | 0 | PASS |
| links | +10.09 | 0 | PASS |

Handles stayed at 88. All 1982 UNLOAD responses (including warmup) returned
success. No diagnostics/warnings were observed in those normal UNLOAD responses.
The production candidate has no successful-call telemetry, so no API call count
or claim of individually observed successful API calls is inferred from silence.

All preserved TCP/lifecycle assertions passed: compiled/runtime artifact hashes,
native SHM fingerprints, expected values for the holdouts (including links),
RUN/FREEZE, UNLOAD/GET_STATE/LOAD and same healthy socket through the lifecycle.
After each lifecycle the original deliberate connection-error/reconnect pattern
was retained. Counters including warmup: 248 duplicate LOGIN terminal closes,
248 partial-frame resets, 248 oversized-frame closes, 247 ordinary disconnects,
991 reconnect checks denying prelogin SHUTDOWN then accepting LOGIN/GET_STATE.
The listener remained usable. This is a 10-minute preliminary gate, not extended
uptime or exhaustive allocation/transport-failure certification.

## Controlled fault paths through real TCP

Four additional owned processes used the same image SHA. These were separate
from the soak. CDB intercepted API boundaries and forced return values/query data
in process memory, leaving the EXE file unchanged. They are injected-failure
tests, not ordinary unmodified executions or spontaneous OS failures.

Before injection, noninvasive CDB examination confirmed the actual Windows API
addresses and the current ntdll PDB's TEB LastErrorValue offset 0x68. ctypes
confirmed PROCESS_MEMORY_COUNTERS_EX PrivateUsage offset 0x48 and size 0x50.
All debugger logs and command files are retained. Only the helper's process
memory-query/HeapOptimizeResources calls were intercepted in the owned process.

| Scenario | Query/API calls | Expected warning detail | TCP result |
|---|---:|---|---|
| Initial query fails | 1 / 0 | read failed, skipped, error 5 | success + warning 1004 |
| Optimization fails | 1 / 1 | API failed/unsupported, error 87 | success + warning 1004 |
| API succeeds, commit remains high | 2 / 1 | remains above threshold | success + warning 1004 |
| Post-call query fails | 2 / 1 | post-read failed, error 5 | success + warning 1004 |

Each response had exactly one expected warning 1004 and success=true; GET_STATE
returned UNLOADED on the same descriptor/peer. After detaching and removing
breakpoints, the same connection completed real PUBLISH, value/native/artifact
checks, UNLOAD, LOAD and UNLOAD without restart. Each server and debugger exited.
No production fault switches, global new/delete hooks, IFEO or registry changes.

Limitations: missing-export/older-OS fallback was not forced through TCP here;
the existing fake-backend tests cover unavailable backend. Normal diagnostic
allocation failure and stderr failure were not injected. Error 87 is a forced
API rejection, not proof of how every unsupported Windows build behaves.

## Integrity and decision

Analyzer recomputes original gates and validates sample/operation/UNLOAD counts,
all successful responses, continuity, exact image SHA, host and fault evidence.
Verification confirms candidate ON/OFF, frozen reference OFF/USE/PGD and HEAP-15
hashes unchanged, plus all 287 frozen source files. Default remains OFF and both
candidate builds have PGO OFF. No C++ or CMake edits were made in this validation.

The previously failing preliminary memory gate now passes on the exact integrated
ON image in the tested environment. Failure-policy behavior has real TCP evidence
under controlled API-boundary injection. No performance acceptance threshold was
introduced; this soak is not a new performance PASS/FAIL experiment. Prior
same-image measurements remain the relevant performance evidence.

Adoption remains a separate decision. This report does not enable the option,
train PGO, publish a release or broaden support claims beyond the tested scope.

Commit packaging: the heap candidate is committed separately from pre-existing
local TCP/PGO/resolver work. The tested executable was built in that working
checkout; the heap-only commit is not asserted to reproduce its complete source
snapshot or binary SHA. A final release source freeze must include and identify
the accepted prerequisites before rebuilding/training PGO.

## Evidence

- `build/unload-heap-release-validation/comparison.json`: consolidated gate,
  Windows host, counts, warnings and limitations.
- `protocol.json`, `windows-host.json`, `verification.json`, `soak-console.log`.
- `run-01-integrated-on/`: executed harness, status, samples, operation timings,
  all UNLOAD responses, client/server output and configuration.
- `tcp-faults/summary.json`, each scenario's result/response, CDB commands/logs.
- `debugger-probe/cdb.txt`: API symbols and verified TEB offset.
- `experiments/unload-heap-release-validation/`: reproducible soak, analyzer,
  debugger probe, isolated TCP fault script and integrity verifier.
