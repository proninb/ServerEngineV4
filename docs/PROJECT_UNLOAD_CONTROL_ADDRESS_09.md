# PROJECT-UNLOAD-CONTROL-ADDRESS-09

2026-10-29. **Capture COMPLETE / INCONCLUSIVE / cause OPEN / NO ADOPTION /
release BLOCKED.** The first-divergence spread from CONTROL-07 was not reproduced
at b03 under synchronized diagnostic observation.

No production ServerEngineV4, original replay.exe, allocator, UNLOAD, TCP, PGO,
trace or earlier report was changed. No concurrency/private-domain experiment.
The user-mentioned ZIP was not attached or found locally; the local harness
implements the specified five-point observer protocol.

## Diagnostic copy and synchronization

The saved THREAD-06 standalone replay source was copied to
`build/project-unload-address-09/replay_address09.cpp`. Only an observer header,
one baseline hook and hooks for boundary IDs 0..3 were added; generation code
and source hashes are saved in source-identity.json. MSVC /O2 /MD /std:c++20
/EHsc /Zi /Zo /DEBUG:FULL build passed.

Diagnostic executable SHA-256:
`949c5748d31cb184e4ebcf488792ad265687391dd7d9d11cf4cce68046ec65ca`.
Original THREAD-06 executable SHA remains:
`e29c95e4d4e4bea399fa532683656f8a39f1e5e737a9b09428056feb35b1c77b`.
Input SHA remains:
`7571f8b57e92fefbf75954794d6d193b6c4e16c781d21528cc61988ed3e0ab8e`.

Only single mode is used. The child emits a checkpoint marker and blocks waiting
for an acknowledgement on stdin. The external harness verifies PID/creation
FILETIME, measures PrivateUsage/handles, walks VirtualQueryEx regions, measures
again and only then resumes replay. No allocator or heap API is called by the
observer hook. The hook does add stdout flushing, stack activity, a pipe and wait
timing; it is not claimed to be observationally invisible.

| Label | Exact placement |
|---|---|
| baseline | Immediately after original initial JSONL sample, metadata already initialized |
| b00 | After original boundary 0 output, before first captured PUBLISH interval |
| b01 | After original boundary 1 output, cycle 0 P_after |
| b02 | After original boundary 2 output, cycle 0 UNLOAD1 |
| b03 | After original boundary 3 output, cycle 0 UNLOAD2 |

The interval b02 → b03 includes LOAD and its following UNLOAD. It is not an
isolated measurement of UNLOAD implementation.

## Six process identities and validation

| Run | PID | Creation FILETIME |
|---|---:|---:|
| 1 | 36856 | 134377929203405371 |
| 2 | 22064 | 134377929225490365 |
| 3 | 34880 | 134377929246962157 |
| 4 | 18288 | 134377929268516516 |
| 5 | 14096 | 134377929290188770 |
| 6 | 29656 | 134377929311932998 |

All five hooks were reached in order in all six processes. Each map rechecks
creation identity against the original process handle. All 30 snapshots have
unchanged PrivateUsage before/after the map walk. This supports quiescence but
does not make a series of VirtualQueryEx calls an atomic OS snapshot.

Each original JSONL record stream, with handshake markers stored separately,
passes the previous **274 boundary signatures** and terminal counters: Alloc
9536484, matched Free 9536471, excluded unmatched Free 159, live 13, logical
worker starts/stops 8781/8781. All exits are zero. Diagnostic/original executable,
input and stage hashes are checked around every run. Nine map/identity contract
tests and Python syntax checks PASS.

## Actual b02 → b03 region deltas

All numbers are bytes. Private region changes are paired only by AllocationBase
**within the same process**, never by matching numeric addresses between processes.

| Run | PrivateUsage delta | MEM_PRIVATE commit delta | MEM_PRIVATE reserve delta | Usage minus commit delta | Changed bases |
|---|---:|---:|---:|---:|---:|
| 1 | -65536 | -65536 | +65536 | 0 | 2 |
| 2 | -155648 | -151552 | +151552 | -4096 | 8 |
| 3 | -155648 | -151552 | +151552 | -4096 | 8 |
| 4 | -155648 | -151552 | +151552 | -4096 | 8 |
| 5 | -569344 | -565248 | +565248 | -4096 | 8 |
| 6 | -155648 | -151552 | +151552 | -4096 | 8 |

Every changed base preserves its combined private commit+reserve extent; actual
region state changes account for most or all of the PrivateUsage delta. Examples:
run 1 base 0x273384b0000 loses 69632 committed bytes while base 0x27335540000 gains
4096; run 2's largest decrease is 151552 bytes at 0x1ad988b0000. Full base records,
before/after commit/reserve and all other changes are in comparison.json.

These are physical virtual-region groupings, **not identification of an NT heap
or C++ owner**. The residual 4096 bytes is retained explicitly; no unmeasured cause
is assigned to it.

## Reproduction gate

| Point | Inter-process PrivateUsage spread | MEM_PRIVATE commit spread |
|---|---:|---:|
| baseline | 57344 bytes | 32768 bytes |
| b00 | 61440 bytes | 32768 bytes |
| b01 | 1212416 bytes | 1216512 bytes |
| b02 | 503808 bytes | 507904 bytes |
| b03 | **155648 bytes / 0.1484375 MiB** | **167936 bytes / 0.16015625 MiB** |

The prior b03 PrivateUsage spread was **2109440 bytes / 2.01171875 MiB**.
The diagnostic series has a >1 MiB spread at b01, but does not reproduce the
specific first-divergence b03 behavior. Thus this is **INCONCLUSIVE**, not a
stability PASS, nor proof of the cause of the historical region difference.

The copy's new executable layout, observer stack/stdio effects, stdin pipe and
stops all differ from the unchanged CONTROL-07 executable. This experiment cannot
assign the reduced b03 spread specifically to the stops. Six processes are also
not a guarantee of sampling every previously observed startup-state group.

## Decision

Do not infer that the reported bases explain CONTROL-07's missing ~2 MiB split,
and do not select a private domain. A follow-up must first separate observation
effects from startup/binary-layout effects before heap-aware attribution can be
linked back to the historical divergence. The present maps are valid observations
of this diagnostic workload only. No further replay was run after the six captures.

Production and frozen OFF/USE/final PGD hashes verified unchanged. No commit,
push or deployment. PGO OFF; release BLOCKED.

## Evidence

Requested result: `build/project-unload-address-09/run-01/comparison.json`.
Each process directory contains 5 region CSVs, 5 map summaries with before/after
memory and process identity, full protocol JSONL, clean validated replay JSONL and
stderr. status.json preserves hashes, exact commands, per-process counters and
all snapshots. Sources: `experiments/project-unload-address-09/`.
