# PROJECT-UNLOAD-THREAD-FIDELITY-06

2026-10-29. **COMPLETE / replay reproduction OPEN / INCONCLUSIVE / NO ADOPTION /
release BLOCKED.** Shared heaps only. No production allocator, UNLOAD, PGO or
ServerEngineV4 changes, and no private-domain replay.

The described ZIP was not attached or found in the workspace; local source implements
the experiment. The existing PID 19288 ETLX remains unchanged.

## Proven common input order

`timeline.jsonl` was generated directly by **one iteration of TraceLog.Events**,
retaining a monotonically increasing source `etl_ordinal` across heap AND thread
events. No merge/sort of the two FIDELITY-05 JSONL files was used. This is the
source trace's common serialized order, not proof of wall-clock ordering of
simultaneously executing instructions on different CPUs.

External PUBLISH/UNLOAD checkpoints were inserted before the first source event
with a strictly later timestamp. No checkpoint timestamp equalled a source event;
otherwise export would fail rather than guess its position. Native events with
equal timestamps retain their original source ordinal. Event fields are checked
against DOMAIN-04 normalization for heap/address, TID, time and operation.

Quality: zero lost events, trace not truncated; PID 19288 and archived validated
creation FILETIME 134376721967437667 checked; diagnostic image SHA remains
`dac808b7dd4f5a72e8eec7ce5e6f8172461d5b3cf2612c38fa15f0e53b50af0d`;
module PDB GUID b99e77e4-59f3-434a-abf9-39dc6920444c, age 1, matches. Identity
provenance remains as documented in FIDELITY-05: creation/full image hash are not
independently reconstructed from a new ETW process-start event.

Strict normalization checks common event order, allocation IDs, live heap/address
pairs, TID generations, duplicate starts, events after stop, Free sizes/owners and
boundary IDs. Result:

| Item | Count |
|---|---:|
| Alloc | 9536484 |
| Total Free | 9536630 |
| Matched Free executed | 9536471 |
| Unmatched Free preserved/excluded | 159 |
| Captured Thread/Start | 8776 |
| Initial/pre-attach generations | 5 |
| Thread/Stop | 8781 |
| Total logical worker generations | 8781 |
| Final live captured blocks | 13 |
| Lifecycle checkpoints | 274 |

Initial thread generations start lazily on their first observed event. This cannot
recover their true pre-attachment startup lifetime. No generation remains active
at the end of the trace.

## Standalone MSVC replay

MSVC x64 /O2 /MD /EHsc /std:c++20 /Zi /Zo /DEBUG:FULL build PASS.

- `single`: the controller executes every captured HeapAlloc/HeapFree. Thread
  start/stop records affect logical lifetime counters but create no OS workers.
- `thread-affinity`: each logical generation has a real `_beginthreadex` worker;
  captured start creates it and captured stop joins it and closes its thread and
  synchronization handles. Initial generations follow the lazy rule above.
- Neighboring operations for the same worker may be batched (up to 1024). A
  controller/worker acknowledgement barrier completes the entire batch before
  any operation for another worker, start/stop, or checkpoint. Therefore captured
  heap operations **never overlap**, and allocation/free order is unchanged.
- Both arms map the same original source heaps to shared NT heaps, enable LFH,
  use the same captured AllocSize and the same initialized VirtualAlloc-backed
  pointer/control metadata. No new/delete interception, TLS allocator or heap trim.
- At each checkpoint, active workers are idle. Actual worker stack committed
  regions are summed with VirtualQuery from each worker's stack AllocationBase.
  This covers worker stacks, not all other non-heap process allocations.

Both arms complete with all count gates matching, 8781 logical starts/stops and
13 live captured blocks. In affinity these correspond to real created/joined
workers; in single they are counters only. All requested worker handles are closed
on successful completion. These are replay processes, not the production server.

## Results and control conflict

All drift values are relative to the arm's initial Private Bytes sample, after
replay metadata initialization.

| Metric | DOMAIN-04 shared | New single | Thread-affinity |
|---|---:|---:|---:|
| Maximum post-first-UNLOAD drift, MiB | 64.343750 | 61.199219 | 18.613281 |
| cycle 70 drift, MiB | 64.343750 | 12.425781 | 8.761719 |
| cycle 80 drift, MiB | 12.699219 | 61.199219 | 8.511719 |
| cycle 90 drift, MiB | 59.667969 | 10.753906 | 4.601563 |

Maximum active-worker stack commit at the post-first-UNLOAD checkpoints is
**0.0625 MiB** for thread-affinity (zero physical worker stacks in single).
Neither new arm exceeds 128 MiB. The source server's >180 MiB post-UNLOAD states
remain unreproduced.

The new single maximum is close to the historical control maximum, but the
high/low phase at cycles 70/80 is reversed. Thus checkpoint-level baseline
comparability is not established. Do not claim that the lower affinity footprint
was caused exclusively by thread identity/lifetime, or that cross-thread Free
caused the original retention. One run per arm does not settle stability.

Because no >128 MiB drift appeared, the conditional heap-vs-stack attribution
and replay SourceID ETW run were not triggered. **No actual replay LFH/main-path
distribution was measured**; LFH enablement is not path-equivalence evidence.
There is no claim that residual Private Bytes equals NT heap commit.

## Validation and next decision

12 normalizer contract tests PASS: record format, explicit lifecycle, implicit
initial generation, TID reuse, duplicate start, event-after-stop rejection, equal
timestamp with preserved order, order rejection, duplicate Alloc, unmatched Free,
cross-thread Free and owner mismatch. Full production-count gates PASS. Both
MSVC replay exit codes 0; complete-record validation PASS.

Decision: **NO ADOPTION**. Preserving thread identities/lifetimes while serializing
heap calls did not reproduce the memory gate failure in this run. The next
investigation, if authorized, must consider actual concurrency and startup heap
state, while first resolving the single-control phase instability. Request-size
and SourceID fidelity also remain open. No private-domain candidate is justified.

Original OFF/USE/PGD hashes remain unchanged. No commit, push or deployment.
PGO default OFF. Release BLOCKED.

## Evidence

Requested result: `build/project-unload-thread-06/comparison.json`.
Other local files: `timeline.jsonl`, `quality.json`, `strict-replay.bin`,
`normalization.json`, `boundaries.json`, `single.jsonl`, `thread-affinity.jsonl`,
stderr and build/export/normalization logs. Sources and reproduction instructions:
`experiments/project-unload-thread-06/`.

Microsoft reference: [_beginthreadex and thread lifetime](https://learn.microsoft.com/cpp/c-runtime-library/reference/beginthread-beginthreadex).
