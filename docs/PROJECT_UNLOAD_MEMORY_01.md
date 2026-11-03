# PROJECT-UNLOAD-MEMORY-01

Date: 2026-10-27. Diagnostic attribution completed; **release remains blocked**.
No production C++ or allocator change was made by this investigation.

## Accepted UNLOAD contract

UNLOAD destroys Project-owned objects, temporary construction data, mappings,
runtime bindings/catalogs, caches and retained Project references/subscriptions.
The listener, healthy client connections and server infrastructure survive.
GET_STATE followed by LOAD or PUBLISH must work on the same connection without
another LOGIN. Disk artifacts survive. Ownership destruction and return to the
existing 128 MiB quiescent private-memory drift budget are separate requirements.
Neither working-set trimming nor restarting the process satisfies this contract.

## Reproducible diagnostic setup

The frozen TCP-PGO-RELEASE-02 source snapshot was copied separately (287 files;
`build/project-unload-memory-01/source-manifest.json`). Release + IPO, PGO OFF,
with /Zi /Zo and /DEBUG:FULL produced an isolated diagnostic executable.
This is not the measured frozen executable: compiler/symbol options and a persistent
TCP connection differ, and diagnostic cycle numbers must not be treated as identical.

Diagnostic image SHA-256:
`dac808b7dd4f5a72e8eec7ce5e6f8172461d5b3cf2612c38fa15f0e53b50af0d`.
PDB GUID `b99e77e4-59f3-434a-abf9-39dc6920444c`, age 1; matching private symbols loaded.
Microsoft-signed Windows 10 SDK Debugging Tools were extracted locally, not installed
system-wide. This avoids the documented Windows 11 SDK UMDH issue.

UST was enabled only for `ServerEngineV4UnloadDiag01.exe`, then removed. The IFEO
key is absent. The owned ETW kernel session was stopped successfully. No diagnostic
server remains running. Frozen OFF, USE and final PGD hashes remain respectively
`6cd312cbc030765718ad0e2fb847723d97efc0395455052cc8a8522843cc3429`,
`509373908a081d913876445ab4c09f54dd072d4e23c16e6fc158b42d1720b554`,
`b3a6fe175e377900959fab72754ee8ffed109b942da09a6fbf93b3feb545a11a`.

## UMDH: live traced heap blocks do not explain the high state

One process, PID 29312, 111 lifecycle cycles; the same logged-in TCP connection
survived both UNLOAD operations, GET_STATE and subsequent LOAD/PUBLISH.
Artifact/native SHM/value checks passed. UST perturbs timing and allocation behavior:
observed high was cycle 70, low cycle 80; fixed cycle 100 was high, not low.

| Capture | Private MiB | Traced blocks | Requested bytes | Overhead bytes |
|---|---:|---:|---:|---:|
| Before first PUBLISH | 1.668 | 770 | 146507 | 35877 |
| Observed high, cycle 70 | 159.723 | 761 | 143171 | 35469 |
| Observed low, cycle 80 | 11.770 | 763 | 143283 | 35565 |

Private Bytes did not change during either UMDH snapshot. High minus low is
approximately +147.95 MiB private commit but **-112 requested heap bytes**.
Changed traced stacks are small TCP/thread-pool allocations; no large positive
Project allocation growth appears in the comparison. This is evidence about traced
heap blocks, not a proof that every Project owner/reference or direct VirtualAlloc
allocation has been eliminated.

## ETW: actual heap decommit observed during subsequent PUBLISH

A separate no-UST process, PID 9500, completed 111 cycles using the same persistent
TCP contract. The trace contains 127073 target-process VirtualMem events, all with
stacks, zero reported lost events and no truncation. Analysis filters the server PID;
the raw system trace is retained locally and is not a portable publication artifact.

| Interval ending after first UNLOAD | Previous quiescent MiB | Current MiB | COMMIT-only MiB | DECOMMIT MiB |
|---|---:|---:|---:|---:|
| cycle 80 | 162.359 | 15.520 | 273.988 | 431.145 |
| cycle 100 | 176.895 | 16.797 | 503.898 | 672.188 |
| cycle 110 | 27.234 | 181.113 | 227.184 | 84.090 |

These are operation-byte totals, not unique live bytes or a complete commit balance:
combined RESERVE+COMMIT and RELEASE are excluded from those two columns. Intervals
include PUBLISH, RUN/FREEZE and first UNLOAD; they are not isolated UNLOAD timings.

A representative large decommit stack in the cycle-100 interval is:

```
server::publish -> construct_full_project -> manifest_composer::compose/visit
  -> basic_string::_Tidy_deallocate -> RtlFreeHeap -> RtlpFreeHeap
  -> RtlpDeCommitFreeBlock -> RtlpCollectFreeBlocks -> RtlpDecommitBlock
  -> RtlpSecMemFreeVirtualMemory -> NtFreeVirtualMemory
```

Thus deferred Windows heap commit/decommit is an observed contributor, not merely
an inference from Private Bytes. The manifest-composer free is the observed trigger
of a heap decommit; it does not identify that string as the owner of all released
pages. This does not prove absence of every leak or satisfy the memory release gate.

## Decision and remaining work

Attribution PASS: the large high/low difference is not explained by growing traced
live heap blocks, and Windows heap decommit during later PUBLISH is directly observed.
UNLOAD memory budget remains FAIL; no deployment and no allocator/heap-trim fix adopted.
All Project ownership postconditions are not yet exhaustively proven.

The next fix needs a bounded Project allocation-lifetime design or another measured
local mechanism that returns its committed pages at UNLOAD, without altering TCP
lifetime. Any candidate requires ownership checks, same-connection lifecycle coverage
and the unchanged external memory gate. These measurements alone do not authorize
replacing the allocator or selecting a particular implementation.

## Evidence and tools

Local evidence root: `build/project-unload-memory-01/`. Key files:
`ust-capture/*-heap.txt`, `high-minus-low.txt`, `high-minus-baseline.txt`,
`etw-control/checkpoints.jsonl`, `virtual-events.jsonl.meta.json`,
`virtual-analysis.json`, `source-manifest.json`, `ust-restored.json`,
`etw-stopped.json`. Harness and reader: `experiments/project-unload-memory-01/`.

Microsoft references: [UMDH preparation](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/preparing-to-use-umdh),
[UMDH comparisons](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/interpreting-a-log-comparison),
[Windows 11 SDK UMDH issue](https://learn.microsoft.com/en-us/troubleshoot/windows/win32/umdh-windows-11-sdk-not-work-fine),
[Virtual memory decommit](https://learn.microsoft.com/en-us/windows/win32/memory/freeing-virtual-memory).
