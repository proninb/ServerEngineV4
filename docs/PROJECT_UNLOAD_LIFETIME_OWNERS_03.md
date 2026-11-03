# PROJECT-UNLOAD-LIFETIME-OWNERS-03

2026-10-28. **Observed heap-allocation lifetime attribution PASS; exhaustive
ownership proof OPEN; memory release gate FAIL; release BLOCKED.**

No production C++, allocator, IFEO, registry, PGO training or existing measured
artifacts were changed. The user-mentioned ZIP was not attached or present in the
workspace; local harness extensions implement the described experiment instead.
This did not repeat UMDH, VirtualMem ETW or CDB captures.

## Source identity and manual ownership review

All 287 files in `build/project-unload-memory-01/source-manifest.json` were
SHA-256 verified against the frozen diagnostic source. This is the frozen
TCP-RELIABILITY snapshot based on e238ae1, including the accepted TCP changes;
it must not be described as byte-identical to the earlier reference commit.
The inventory contains 210 regex review leads and deliberately gives no automatic
C++ ownership PASS.

Reviewed boundaries in that snapshot:

| Lifetime | Source evidence | Interpretation |
|---|---|---|
| Construction | `project_lifecycle_context.hpp`, `full_construction_context`; `project_full_construction.cpp:1015` | Local aggregate owns files, lexical, strings, identities, G, source map and assigns. Normal return/RAII exception unwinding destroys it. |
| Configuration | `project_configuration_manifest.cpp:1060,1083` | Local manifest composer, not a resident Project field. |
| Resident | `server_context.hpp:64`, `project.hpp` | unique_ptr owns Project; its fields own mappings, SHM, runtime binding index and IC catalog. |
| UNLOAD | `server.cpp:2109–2141` | Valid-state check followed by project.reset; communications remain server-owned. |
| Server/TCP | `communication_connection.cpp:419,466,514` | Wait callback captures connection. Subscription registry is connection-owned; no production add_subscription call site was found beyond the implementation. |

No specific retained Project-reference chain was established. Regex absence is
not a whole-program escape proof; dormant subscription APIs and future callers
still require explicit lifetime design.

## External heap trace and quality gate

Separate diagnostic process PID **19288**, creation FILETIME **134376721967437667**.
The helper checked process creation identity before attaching heap tracing.
Image SHA-256:
`dac808b7dd4f5a72e8eec7ce5e6f8172461d5b3cf2612c38fa15f0e53b50af0d`.
Private PDB GUID `b99e77e4-59f3-434a-abf9-39dc6920444c`, age 1, matched and loaded;
3323 of 3325 application addresses resolved to symbols.

After LOGIN, before first PUBLISH, the helper started its own BASE kernel session
and named `SEV4HeapLifetime03` heap session for only this PID, with allocation and
reallocation stacks. Existing session conflicts would abort rather than stop
another capture. Both owned sessions stopped with exit 0; merge exit 0.

91 same-TCP cycles completed in 83.216 seconds. The adaptive checkpoints were
high cycle 70, low cycle 80, repeated high cycle 90; these numbers were observed,
not prescribed. Artifact, native SHM, expected values, GET_STATE and subsequent
LOAD/PUBLISH passed without reconnect. Handles stayed 88. No performance claim
is made for an instrumented run.

Trace gate: zero lost events, not truncated, **9536484 Alloc / 9536630 Free**.
Allocation stacks were present for 9536474 Alloc events (10 missing stacks).
The trace contains no Realloc events. Free stacks were not requested; Free event
heap/address keys suffice for pairing. Reader rejects an unimplemented Realloc
schema rather than silently ignoring it.

## Address-paired live blocks at the boundaries

The reader keys live allocations by **(HeapHandle, address)**, removes them on
Free, and samples at external UTC checkpoints. No live key was overwritten.
159 Free events had no matching captured Alloc, consistent with a trace begun
after process startup. Those are explicitly excluded from ownership conclusions.
Event size is the provider's AllocSize; do not equate it to exact C++ requested size.

| Boundary | Cycle | Private MiB | Captured live blocks | AllocSize bytes |
|---|---:|---:|---:|---:|
| Before first PUBLISH | -1 | 1.211 | 15 | 5380 |
| After PUBLISH | 70 | — | 22 | 4115308 |
| After UNLOAD, high | 70 | 187.332 | 17 | 5779 |
| After PUBLISH | 80 | — | 22 | 4115308 |
| After UNLOAD, low | 80 | 20.109 | 17 | 5779 |
| After PUBLISH | 90 | — | 22 | 4115308 |
| After UNLOAD, repeated high | 90 | 185.805 | 17 | 5779 |

The five resident-origin blocks present after each selected PUBLISH are:

| Allocation stack family | Blocks | AllocSize bytes |
|---|---:|---:|
| prepare_runtime_bindings → vector<unsigned int> | 2 | 3007210 |
| prepare_runtime_bindings → vector<uint64_t> | 1 | 1100711 |
| make_unique<project> | 1 | 1448 |
| Project path string | 1 | 160 |
| **Total removed by subsequent UNLOAD** | **5** | **4109529** |

No other live captured allocation with an application Construction origin remains
at those PUBLISH-complete boundaries. None of these resident-origin blocks remains
at the corresponding post-UNLOAD boundary. Stack ancestry through
construct_full_project alone is not called Construction ownership: the resident
factory/binding calls distinguish the allocations in the table.

After UNLOAD all three states have the same five stack groups:
13 Unknown/no-app-stack blocks, 5268 bytes; server-state outbound queue 280 bytes;
TCP/output string 119 bytes; TCP run-loop 64 and 48 bytes. Unknown remains Unknown,
not assigned to Project or TCP without evidence. The 167.222656 MiB high/low private
difference has no corresponding difference in captured live allocations.

## Decision

For the measured scenarios, observed construction-origin allocations have ended
by PUBLISH completion and observed resident allocations end at UNLOAD, while the
large private commit persists independently. This gives stronger lifetime evidence
than a destructor/reset audit alone and supports investigating Construction memory's
physical allocation domain. It does not prove the historical origin of every
retained heap page or the complete allocation history before attachment, direct
VirtualAlloc ownership, all possible error paths, or every C++ reference/subscription.

Do not add arbitrary clear/reset calls to UNLOAD on this evidence. A future bounded
allocation-domain candidate needs explicit accounting for Construction, Resident
and Server lifetimes, error unwinding and threads, and must pass the unchanged
128 MiB private drift / 16 handle drift gate plus same-TCP lifecycle. No allocator
or production candidate was selected here. PGO default OFF; no deployment.

## Reproduction and local evidence

Scripts under `experiments/project-unload-memory-01/`: `audit_owners.py`,
`heap_trace_helper.ps1`, `run.py --heap-trace`, `heap-reader`, `live-reader`.
Local root: `build/project-unload-memory-01/lifetime-owners-03/`.
`source-audit.json` contains the verified static inventory.
`trace-01/` contains target identity, executed harness, external boundaries,
adaptive maps, status, event inventory, `live-checkpoints.jsonl`, `live-meta.json`,
symbol-resolution log, and explicit recorder stop results. Raw ETL is local only;
it includes system metadata and is not a portable report attachment.
The archived harness reused an identity block that incorrectly set `cdb_enabled`
true for heap tracing; no CDB command was executed in this run. The harness flag
has been corrected for future captures; archived executed code/status are preserved.

The harness and helper are a coordinated pair: create an empty fresh run folder,
start the elevated helper waiting for target.json, then start the unsandboxed
harness with --heap-trace. The helper aborts after 15 minutes or an 8 GiB trace;
it stops only the sessions it owns. Existing trace data must not be overwritten.

Microsoft documents [heap capture on an existing process](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/xperf/quick-start---enabling-data-capture-on-an-existing-process).
