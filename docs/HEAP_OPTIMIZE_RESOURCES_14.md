# HEAP-OPTIMIZE-RESOURCES-14 — real ServerEngineV4 diagnostic OFF/ON

2026-10-30. **Preliminary mechanism candidate POSITIVE / OFF memory FAIL /
ON memory PASS_PRELIMINARY / no production adoption / release BLOCKED.**

The new user request explicitly authorizes this diagnostic HeapOptimizeResources
test, an exception to the previous no-trim diagnostic restriction. It does not
authorize production integration, a new allocator or changing release acceptance.
The test is on the real ServerEngineV4 TCP executable, not standalone replay.

## API verified against Microsoft documentation

HeapSetInformation with HeapOptimizeResources and a null handle optimizes caches
of the process's LFH heaps and decommits memory **if possible**. A non-null handle
restricts the target. HEAP_OPTIMIZE_RESOURCES_INFORMATION is initialized with
HEAP_OPTIMIZE_RESOURCES_CURRENT_VERSION and zero Flags. The class/structure are
available starting with Windows 8.1. BOOL success does not guarantee a particular
Private Bytes reduction or identify the original C++ owner of released pages.

Sources: [HeapSetInformation](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapsetinformation),
[resource information structure](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-heap_optimize_resources_information).

## Isolation and exact patch

All 287 frozen TCP-source manifest files were verified, copied separately under
`build/heap-optimize-resources-14/source`, and left unchanged at their original
locations. Only the copy of server.cpp changes: include one diagnostic header and
call after_unload immediately after successful UNLOAD's project.reset, before
publishing UNLOADED. The header is the only added source file. No parser, G,
format, runtime plan, SHM materialization or production source change.

One diagnostic Release + IPO, PGO OFF executable serves **both** arms:
SHA-256 **c353b8992db1c03104f7df59d1b3c0c32460c7b70bf48bc271d67f198ee4760e**.
Four-character selector SE_HEAP_OPT14=OFF_ / ON__; missing selector defaults OFF.
Both arms use the same memory queries and logging; only ON invokes the API.
Telemetry records selector validity, called/success/error separately, QPC interval
and PrivateUsage directly before/after the API. OFF success=false means not called.
Log writes and file open/close occur after that interval.

MSVC x64 /Zi /Zo /DEBUG:FULL /INCREMENTAL:NO, Release/IPO build PASS. No PGO profile
was used or trained. Pilot of four fixtures in both modes passed native/artifact/
value checks and same-connection lifecycle; all eight pilot ON calls succeeded.

## Unchanged real-server soak protocol

The archived TCP-PGO-RELEASE-02 soak was adapted only for diagnostic paths,
selector, external UNLOAD timing logs and same-socket assertions. Source archive
hash and exact modifications are preserved in harness-origin.json.

- OFF followed by ON, sequential independent server processes of the **same image**.
- Ten warmup cycles, then at least 600 measured seconds for each arm.
- Same fixture order: UnitProXL every tenth cycle; strings/inheritance/links otherwise.
- PUBLISH → artifact/native SHM/value verification → RUN/FREEZE → UNLOAD → LOAD →
  native SHM verification → UNLOAD, all on the same healthy TCP connection.
- The original intentional duplicate LOGIN/partial frame/oversize/disconnect and
  reconnect/prelogin-denial pattern follows each lifecycle. Therefore this is not
  an unbroken single connection for the entire 10 minutes; UNLOAD itself never
  requires or causes reconnect.
- Same external UNLOADED/reconnected checkpoint and per-fixture **last-five minus
  first-five measured median**, <=128 MiB Private Bytes drift / <=16 handle drift.
  No working-set substitute, peak-based replacement, altered threshold or window.

OFF's final assertion failure is the expected memory-gate FAIL after all correctness
cycles complete. Driver continues to ON only when the full per-fixture gate data
exists; a correctness/incomplete-soak failure would stop the experiment.

## Results

| Metric | OFF | ON |
|---|---:|---:|
| Measured duration | 603.174 s | 605.711 s |
| Total / measured cycles | 991 / 981 | 921 / 911 |
| Successful UNLOAD telemetry rows | 1982 | 1842 |
| UnitProXL Private Bytes drift | **239.890625 MiB — FAIL** | **2.027344 MiB — PASS** |
| Strings drift | 20.531250 MiB | 2.343750 MiB |
| Inheritance drift | 20.941406 MiB | 2.355469 MiB |
| Links drift | 22.730469 MiB | 2.339844 MiB |
| Handles observed / drift | 88 / 0 | 88 / 0 |
| Quiescent Private Bytes min–max | 13.035–261.121 MiB | **3.961–6.492 MiB** |
| Quiescent Private Bytes median | 231.840 MiB | 5.859 MiB |
| API calls / failures | 0 / 0 | **1842 / 0** |
| Maximum immediate Private Bytes drop | 0 | **247.968750 MiB** |

All artifact/native SHM/expected value/state/TCP assertions completed in both arms.
Log sequences are contiguous and exactly twice the completed cycle count; both
memory-query validity flags, timer validity and selector/called flags pass for
every row. Analysis independently recomputes the original per-fixture gate from
raw samples and matches the harness result. OFF reproduced the failure rather
than merely comparing a successful ON to an unrelated historical image.

## Cost of the call and end-to-end latency

| Timing | Median | p95 | Max |
|---|---:|---:|---:|
| ON API interval, all UNLOADs incl warmup | **0.4829 ms** | **2.4130 ms** | **22.0334 ms** |
| OFF measured TCP UNLOAD | 0.4475 ms | 290.5301 ms | 470.8910 ms |
| ON measured TCP UNLOAD | 1.0042 ms | 316.0021 ms | 449.3799 ms |

API interval includes its immediate setup/error capture plus QPC bracketing, but
not telemetry file I/O. TCP UNLOAD includes resident destruction/unmapping,
dispatch/response and network/client effects, not just this API. Heavy UNLOAD tails
already exist in OFF; do not attribute the entire TCP duration to optimization.

The measured full-cycle intervals also differ: median UnitProXL lifecycle
**5.6809 s OFF vs 6.2283 s ON**. They include PUBLISH, artifact/native validation,
RUN/FREEZE, both UNLOADs, LOAD and reconnect/error checks. Fewer ON cycles finish
within the same requested time. This is an important observed tradeoff, but a
single sequential pair cannot isolate its causes or establish native PUBLISH
performance. In particular, unchanged algorithms do not imply unchanged allocator
reuse/page-fault cost. A dedicated performance gate remains necessary.

## What the result establishes, and what it does not

There is now a concrete, effective **preliminary real-server candidate** for the
observed retained-commit workload: enabling the API in the same image turns an
OFF failure into ON passing the unchanged coarse memory gate, with large immediate
PrivateUsage reductions and no observed functional regression in the executed checks.
This justifies considering the API before a broad Construction allocator rewrite.

Limits remain:

- One OFF-then-ON pair, not alternating replicated runs; time/OS variation and
  different cycle counts remain. It is a 10-minute preliminary sample per arm,
  not long-duration or universal memory certification.
- nullptr affects process LFH heaps, including Server/TCP caches. The result is
  not a proof that every reclaimed page belonged to Construction or that all
  Project references have been exhaustively eliminated.
- PrivateUsage before/after is direct commit-charge evidence, not a new mapping/
  heap-owner trace. No new ETW or VirtualQueryEx attribution is claimed.
- API success and this gate PASS do not turn best-effort decommit into a guarantee
  on every Windows heap state/project. Contract-12 ownership/backing coverage and
  failure-path questions are not automatically closed.
- API failure/unsupported OS policy, operational placement and broad latency/
  performance regression coverage require a separately reviewed actual candidate.
  Diagnostic calls log API failure without changing the existing UNLOAD result.

No _heapmin, HeapCompact, HeapDestroy, EmptyWorkingSet, LFH enabling, allocator
override, restart workaround or threshold change was used. _heapmin is not needed
to interpret this positive first API test and was not tested.

## Decision and frozen references

**Diagnostic candidate positive; production integration NOT PERFORMED.** Release
remains BLOCKED because the accepted production source/binary still lacks the
change and this first test does not replace a release gate for a new source/PGO
candidate. PGO default OFF; frozen OFF/USE/final PGD hashes match their recorded
values. No commit, push or deployment.

## Artifacts

`build/heap-optimize-resources-14/comparison.json` is the machine-readable result.
Other evidence: pilot.json, driver-status.json, source-before.json, source-delta.json,
harness-origin.json, verification.json, configure/build logs and both soak folders.
Each soak folder contains the executed harness, original status/samples, external
UNLOAD timings and server/heap-optimize.jsonl. Successful server processes were
closed through their ordinary shutdown path after each completed arm.

Source harness/diagnostic header: `experiments/heap-optimize-resources-14/`.
Production ServerEngineV4 and original replay remain unchanged.
