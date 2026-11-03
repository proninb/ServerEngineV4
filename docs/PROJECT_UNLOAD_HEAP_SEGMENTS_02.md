# PROJECT-UNLOAD-HEAP-SEGMENTS-02

2026-10-27. **Heap-domain attribution PASS / lifetime-owner attribution OPEN /
release BLOCKED.** No production C++ or allocator change.

Four adaptive snapshots were captured in one diagnostic process, PID 29644:
baseline, first high, subsequent low and repeated high. This used the previously
symbolized diagnostic image, SHA-256
`dac808b7dd4f5a72e8eec7ce5e6f8172461d5b3cf2612c38fa15f0e53b50af0d`.
CDB 10.0.19041.5609 attached noninvasively by PID to the running process, executed
`!heap -s`, `!heap -stat`, `!address`, and exited. No UST, ETW or registry changes.
An initial sandbox attempt failed before capture with loopback WinError 10013;
the completed run was outside sandbox. It did not repeat UMDH/ETW.

## Measured result

All heap-tagged private regions belong to NT heap **0x000001fea41f0000**.
The table uses actual address-region state from `!address`, not the potentially
inconsistent internal counters in `!heap -s`.

| Snapshot | Cycle | Private Bytes MiB | Actual heap commit MiB | All MEM_PRIVATE commit MiB |
|---|---:|---:|---:|---:|
| Baseline | -1 | 1.1875 | 0.207031 | 0.429688 |
| High | 60 | 175.621094 | 173.988281 | 174.347656 |
| Low | 66 | 16.226563 | 14.593750 | 14.953125 |
| Repeated high | 70 | 185.777344 | 184.140625 | 184.500000 |

High minus low: **159.39453125 MiB**, exactly equal for Private Bytes, this heap's
actual committed regions, and all MEM_PRIVATE committed regions. Other private
committed regions total 0.359375 MiB in both states. The heap's combined committed
and reserved region extent remains 266203136 bytes between high and low; its
167137280-byte commit reduction becomes reserve. External VirtualQueryEx maps
independently give the all-private totals above.

Private Bytes and handle counts were identical before and after each debugger
capture. Handles stayed 88. The persistent client completed 71 cycles, including
both UNLOADs, GET_STATE, LOAD/PUBLISH and native SHM/artifact/value checks without
reconnect or repeat LOGIN. Process creation FILETIME, PID and executable hash are
recorded and verified by the comparator. Diagnostic pauses exclude this run from
performance comparisons or replacement of the original release gate.

## Important counter limitation

At high, `!heap -s` reports Commit 178164 KiB and Free 166227 KiB. At low it still
reports Commit 178164 KiB (Free 165972 KiB), despite the externally confirmed
decommit. `!heap -stat` similarly retains a large committed-byte counter. These
internal counters therefore cannot be used as authoritative actual commit in this
trace. Subtracting Free from Commit is not proof of live C++ allocation size.
The plain `!heap -stat` output here provides heap aggregate fields, not a complete
busy-allocation histogram. That part of the proposed analysis remains incomplete;
no successful busy-allocation attribution is claimed.

## Interpretation and next boundary

This run ties the high/low difference to a concrete NT heap's physical regions.
Together with PROJECT-UNLOAD-MEMORY-01's UMDH and decommit stacks, it supports
investigating shared-heap retention and lifetime isolation. It does **not** identify
the complete composition of live owners, prove every Project reference is gone,
or distinguish Construction and Resident Project allocations within that heap.
The previous experiment's 147.95 MiB and this run's 159.39 MiB are different runs,
not a changed baseline or gate.

Before selecting a lifetime allocation domain, trace which Construction, Resident
and Server owners allocate into the identified heap and audit destruction/reference
boundaries. No allocator replacement, forced trim, restart workaround, threshold
increase, commit, push or deployment was performed by this experiment.

## Reproduction and evidence

Capture: `experiments/project-unload-memory-01/run.py --cdb`; comparator:
`experiments/project-unload-memory-01/compare_heaps.py`.
Local evidence: `build/project-unload-memory-01/heap-segments-02-native/`:
`status.json`, `executed-harness.py`, `heap-comparison.json`, four `*-cdb.txt` logs,
four `*-regions.csv` maps, map summaries and `checkpoints.jsonl`.
The comparator fails on unsupported/missing heap summary or address data, incomplete
capture, or identity mismatch. Raw diagnostic evidence remains local.

References: [Microsoft noninvasive debugging](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/noninvasive-debugging--user-mode-),
[Microsoft !heap commands](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/-heap).
