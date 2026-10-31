# Inactive UNLOAD heap optimization candidate

2026-10-30. CANDIDATE PREPARED / DEFAULT OFF / NO ADOPTION / RELEASE BLOCKED.
PGO remains OFF. No deployment, commit or push.

## Implementation contract

`SERVER_ENGINE_UNLOAD_HEAP_OPTIMIZATION` is a CMake option, default OFF. OFF does
not compile the UNLOAD hook and does not query process memory. There is no runtime
environment-variable override in the candidate. Enabling requires a deliberate
separate build, for evaluation only until adoption is approved.

After successful `context.project.reset()` and transition to UNLOADED, the optional
noexcept helper reads PrivateUsage. At >=120635392 bytes it performs one
`HeapSetInformation(nullptr, HeapOptimizeResources, initialized_info, sizeof(info))`.
Threshold equality triggers. The threshold is the existing workload calibration,
not a universal server budget or a replacement release gate. Process-wide LFH
optimization can affect Server/TCP heaps as well as former Project allocations.
No parser, G, SHM, TCP ownership, allocator, arena or UNLOAD precondition changes.

No Project references are retained. No retries, forced trim, process restart or
alternative allocator fallback. A successful API return does not guarantee the
target commit: the helper reads memory again, reporting a warning when commit
remains >=threshold. That observation is not a proof of leakage.

Failures are warnings with stable diagnostic `server.heap_optimization_warning`
(1004), operation ID, stage, Win32 error and before/after/threshold bytes:

- Missing platform/API entry point: skip.
- Initial memory read failure: skip, never optimize using an invalid measurement.
- API rejection/failure (including unsupported information class): no retry.
- Post-call read failure: report measurement failure.
- Remaining high commit: report unmet candidate target.

All leave UNLOAD successful and the TCP connection intact. Diagnostics are best
effort: if allocation for the normal diagnostic throws, a fixed-buffer stderr
fallback records the failure; it cannot turn completed UNLOAD into failure.
No claim of guaranteed logging under OS/device failure is made.

## Windows compatibility

The optional backend dynamically resolves K32GetProcessMemoryInfo and
HeapSetInformation from the already loaded kernel32.dll; it loads no new module
and retains no module handle. Missing exports skip with warning. Older Windows
can export HeapSetInformation yet reject HeapOptimizeResources: that returned
error is preserved and warned, with no substitute optimization. Non-Windows
enabled builds also skip with warning.

HeapOptimizeResources requires Windows 8.1 / Windows Server 2012 R2. Current
Windows SDK declarations are required to compile the Windows backend. Actual
older-OS execution was not tested; unavailable/backend failures are injected in
unit tests. Microsoft describes decommit as possible, not guaranteed:
[HeapSetInformation](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapsetinformation),
[HEAP_OPTIMIZE_RESOURCES_INFORMATION](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-heap_optimize_resources_information).

## Candidate verification

Separate MSVC Release+IPO builds, PGO OFF:

- OFF: `111fbe96e7eefaad677e488667a448d2da75df5ab56bc2e185568b17669123b3`.
- ON: `a4c3e005998e463b66aefd4e9c96ac6e67274f14657a46e293687af51a4282d9`.

Build paths: `build/heap-integration-candidate/off` and `on`. OFF/ON are separate
correctness candidates, not a controlled performance comparison between images.

`unload_heap_optimization` CTest passed in both builds. It checks disabled mode
has no backend access, missing API, memory-read failure, threshold boundary,
successful optimization, API failure, post-read failure and insufficient release.
Failure results emit warning severity without diagnostic errors, preserving the
operation ID. It also checks real Windows memory-query entry points. Injection
is confined to a fake backend in the test process; no production fault switches
or global new/delete overrides. Diagnostic-allocation failure is caught in code
but was not artificially injected.

Existing `communication_regressions` CTest passed. TCP candidate parity passed
OFF and ON on strings/inheritance/links/UnitProXL: compiled/runtime images,
native SHM, expected values, RUN/FREEZE/UNLOAD/GET_STATE/LOAD and the same socket.
Three consecutive heavy PUBLISH/UNLOAD operations also passed for each candidate.
Evidence: `build/heap-integration-candidate/parity.json`, CTest logs and build logs.

This is not a new 600-second memory gate, full release gate or new PGO profile.
Injected backend failures were tested at helper/diagnostic level, not as real
Windows API failures delivered through a live TCP server.

## Targeted direct PUBLISH measurement

Original unchanged HEAP-15 executable, SHA-256
`87d8096a00658457ebafebc21477dabbf20cae3fe0084280ea2cbdc6932226af`.
Eight fresh processes, OFF/POLICY/POLICY/OFF repeated twice, three warmups then
twelve measured UnitProXL publications each (96 measured total). Every PUBLISH
was followed by validated artifacts/native SHM and UNLOAD. Between UNLOAD and
the next PUBLISH only GET_STATE and external memory observations occur: no LOAD,
other fixture or reconnect. The same TCP descriptor/peer is checked.

| Run | Mode | PUBLISH median / p95 ms | Immediately after actual cleanup: n / median ms |
|---|---|---:|---:|
| 01 | OFF | 1300.09 / 1329.22 | 0 |
| 02 | POLICY | 1302.53 / 1357.59 | 5 / 1287.82 |
| 03 | POLICY | 1301.56 / 1388.37 | 5 / 1297.58 |
| 04 | OFF | 1311.80 / 1360.10 | 0 |
| 05 | OFF | 1305.10 / 1357.25 | 0 |
| 06 | POLICY | 1303.10 / 1357.75 | 6 / 1302.22 |
| 07 | POLICY | 1295.21 / 1336.63 | 5 / 1290.48 |
| 08 | OFF | 1297.38 / 1332.92 | 0 |

There are 21 measured UnitProXL PUBLISH observations immediately following a
successful actual cleanup. No large latency penalty is apparent in this small
sample. This is not a randomized cleanup/no-cleanup comparison within a process:
calls select high-commit states. Per-run p95 with only 12 observations is the
sample maximum and is weak tail evidence. No equivalence, speedup or performance
acceptance threshold is asserted. Raw faults and quiescent measurements are kept.
The targeted short test does not replace the existing 600-second memory gate.

Evidence: `build/heap-direct-publish/comparison.json`, `verification.json`,
`protocol.json`, per-run samples and API logs. Scripts are in
`experiments/heap-optimize-performance-16/direct_publish.py`, `verify_direct.py`
and `candidate_parity.py`. Frozen OFF/USE/PGD and HEAP-15 hashes are rechecked.

## Remaining adoption gates

Explicit approval is still required to enable the candidate. Supported
configuration/OS validation, a release memory/lifecycle gate on the new integrated
source snapshot, and release-policy review of warning-only failure behavior
remain open. New PGO training must use that final snapshot if PGO is selected.
Existing unrelated worktree modifications were preserved; no rollout occurred.

### Validation follow-up, 2026-10-30

The exact integrated ON image subsequently passed the preliminary 600-second
memory/lifecycle gate on Windows 11 Home x64 build 26200. Four controlled
API-boundary fault scenarios also passed through real TCP in separate owned
processes. See [release validation](UNLOAD_HEAP_RELEASE_VALIDATION.md) for results,
scope and limitations. This closes the preliminary integrated-image gate for
that tested configuration; adoption remains unapproved and default/PGO stay OFF.
