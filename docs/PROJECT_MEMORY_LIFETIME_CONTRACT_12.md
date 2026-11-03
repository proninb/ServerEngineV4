# PROJECT-MEMORY-LIFETIME-CONTRACT-12

2026-10-29. Architecture review and physical-memory contract; **no production-code
change or implementation selected**. Diagnostic/replay chain 01–11 is concluded;
its evidence is preserved, and no further standalone replay is the default next step.
Logical ownership is supported within reviewed paths; exhaustive ownership remains
OPEN. Physical budget implementation is OPEN. PGO OFF; release BLOCKED.

## Scope and evidence identity

Reviewed local PROJECT.md, SERVER_ARCHITECTURE.md and 23 implementation/header files.
Hashes and HEAD are saved in
`build/project-memory-lifetime-contract-12/review-inputs.json`. Reviewed source
files present in the frozen diagnostic snapshot match it byte-for-byte. The tree
includes the already accepted TCP reliability changes; this is not a claim that
every working-tree file equals the historical e238ae1 Git commit.

Existing documents establish logical lifetimes, not a verified allocator/backing
contract satisfying the unchanged 128 MiB quiescent drift gate. This document
formalizes the missing requirement and acceptance obligations, not a declaration
that the current implementation already satisfies it. Canonical architecture
documents and production sources are left unchanged by this review.

## 1. Ownership matrix

Allocation origin in time is not ownership: an allocation made during PUBLISH
may be Resident or Server-owned. The owner is determined by the resource's last
valid consumer and explicit transfer, never by thread identity alone.

| Class | Concrete resources | Logical owner | Required end / permitted transfer |
|---|---|---|---|
| Construction, full-source | manifest composition, acquisition buffers, file_context, lexical_generation, string_table, identity_space, graph G, source_map, assign_table, preprocessing state | Local full_construction_context plus operation-local helpers | End of PUBLISH/REBUILD, on success or failure; no borrowed view may escape into resident state |
| Construction, physical preparation | runtime/shm layout, shm_runtime_v2, plan vectors, parser scratch, lane results, worker/job contexts, temporary writable/read-only persistence mappings | Synchronous operation/helper scopes, including locals outside full_construction_context | Last synchronous consumer; all gone before return. Publication may overlap their existence but must not retain them |
| LOAD/BUILD temporary | load/build contexts, persisted acceleration views, graph/source deltas, acquisition and runtime preparation scratch | Mode-specific local operation scopes | End of that operation; BUILD's persisted-lineage semantics remain unchanged |
| Resident | project object/path, final compiled mapping+view, SHM mapping, runtime_binding_index vectors/scalars, IC catalog/path | server_context.project → project | UNLOAD; only detached resident-owned storage and references to resident-owned images may cross PUBLISH completion |
| Server | configuration/settings, license/lease/authentication, communication registry/listeners, request infrastructure, bounded server diagnostics/state | server_context and subsystem owners | SHUTDOWN or explicitly bounded request/session lifetime; must not retain construction contexts or scale unboundedly with historical G |
| Connection/request | session, transport state, outbound owned payloads, outstanding request IDs, registrations, response-write synchronization | Communication/connection/request owners | Connection close or request completion; healthy connections survive UNLOAD |
| Project registration carried by a connection | Publisher subscription and any borrowed runtime target | Registration has a connection holder but a Project scope | Detach from Project before Project destruction; connection itself remains valid |
| Unknown | Unclassified heap blocks, callbacks with unproven target lifetime, pre-trace allocations | Not assigned without evidence | Must remain Unknown until identified; not silently charged to Server to bypass Project teardown |

Construction is an **operation-local** owner; resident project does not contain
full_construction_context. The logical Server/Project/Construction model is
preserved without inventing a resident Project → Construction ownership edge.

## 2. Observed implementation boundaries

- project_lifecycle_context.hpp declares full_construction_context and its owned
  members. project_full_construction.cpp:1015 creates it locally on the ordinary
  path. The CW_CONSTRUCTION_EARLY_RELEASE_AB branch at 1591–1595 is experimental;
  ordinary RAII destruction is before PUBLISH returns, not guaranteed early decommit.
- project_configuration_manifest.cpp:253–264 scopes acquisition std::jthreads;
  normal exit and unwinding join before captured pending/file state goes away.
  manifest_composer is local at 1060/1083.
- runtime/project_runtime.cpp:1524–1561 prepares detached runtime bindings,
  resets v2_runtime/v2_layout at 1550–1551, then creates project and moves final
  compiled mapping, SHM and bindings. Runtime bindings contain scalar offsets and
  owned vectors, not references to the temporary layout.
- project.hpp/project.cpp own resident resources directly. compiled_view borrows
  the moved compiled_mapping; system()/shm() return borrowed access into SHM.
  ic_catalog holds owned vector/string/path entries.
- server_context.hpp:64 is the unique_ptr owner. server.cpp:2109–2141 validates
  UNLOAD then calls project.reset() before publishing UNLOADED. Communication is
  stopped at SHUTDOWN, not ordinary UNLOAD. Valid states in code include LOADED
  and FREEZE; some older architecture text abbreviates this as LOADED.
- read_only_file_mapping.cpp, writable_file_mapping.cpp and fixed_shared_memory.cpp
  reset mapped views/handles through RAII. Their destruction releases this process's
  ownership; it cannot require another process to close its independently owned map.

These findings do not constitute an exhaustive proof of every exception, native
wait failure, externally borrowed pointer or future extension. No concrete extra
clear/reset defect in UNLOAD was established by this review.

## 3. Borrowing and worker barriers

Before a lifetime boundary is physically released:

1. Stop admitting new work targeting that owner. Every outstanding reader/writer,
   callback and queued borrowed view must finish or detach. A state enum change
   alone is not proof that memory users are quiescent.
2. Finish physical-file readers while file_context/path/input buffers remain
   alive. std::jthread scopes must unwind before their captures' owners.
3. Complete parser/persistence/runtime materialization jobs before destroying job
   structs, layouts, input images or destination buffers. execution_lanes::run
   waits for started lanes and clears stored function/context; pool destruction
   signals stop and joins/closes platform workers. These pools are persistent
   within an operation, not evidence of a Server-lifetime construction pool.
4. Dispose of borrowed views before their backing mapping/domain. Handles and
   offsets may escape only with the resident image/lifetime that interprets them.
5. Keep Server settings alive through all operations borrowing const settings
   references. Never put worker TLS or Server caches in a shorter-lived domain.

Normal synchronous barriers are visible in the reviewed code. WaitForSingleObject,
SetEvent, partial worker startup and exceptional shutdown failure paths require
separate fault coverage before a backing-release guarantee is claimed. Some
native cleanup return values are ignored; this review does not convert that into
an established use-after-free defect or claim all such paths safe.

### Transfer rule

Moving a vector changes its C++ owner, not its physical backing origin. If a
future Construction backing domain owns an allocation, moving that vector into
project and then releasing Construction backing would violate this contract.
runtime_binding_index and project/path/IC storage must be Resident-owned at first
allocation or explicitly detached into Resident storage before release. The final
G continues through the existing packed image/mapping publication boundary;
no second full G, new Semantic Model or format change is required by this contract.

## 4. Project-scoped subscriptions and queued messages

Current subscription_registration is a move-only holder of **void* context +
unregister callback**. Its destructor/reset invokes that callback. Connection
subscriptions are cleared by Communication close/registry teardown, not by
server::unload(). No production add_subscription() call site was found beyond
its implementation; tests exercise it. Therefore this is a **contract gap for
Project publishers**, not a demonstrated explanation of current retained commit.

Required future Project-publisher contract:

- Every registration explicitly identifies its scope: Server/connection or a
  particular Project lifetime. No untyped callback is assumed safe just because
  its holder is connection-owned.
- Before Project publisher/backing destruction, prohibit new deliveries, detach
  its connection registrations exactly once and finish in-flight callbacks.
  unregister must run while its target remains valid; later connection close
  must not invoke a stale Project callback.
- Queued outbound data must own its serialized bytes or a lifetime-safe reference;
  it cannot contain naked spans/pointers into released SHM, compiled mapping or
  Construction storage. Late deliveries from a retired Project must not become
  notifications for a subsequent LOAD's Project.
- Request/session/connection state and legitimate Server-scoped subscriptions
  remain. Do not tear down every connection or clear unrelated registrations as
  a blanket UNLOAD implementation.

Existing protocol/outbound payloads use owned strings/vectors in reviewed
structures. This is useful evidence, not a guarantee about future callback users
or every byte stored inside an opaque payload.

## 5. Physical-memory contract (required capability, implementation OPEN)

Two independent postconditions are required:

**Logical:** no live Construction allocation/borrower after operation return;
no Project-owned object/mapping/registration/borrower after UNLOAD.

**Physical:** after the established settled lifecycle checkpoint, process Private
Bytes obeys the unchanged **<=128 MiB drift** gate and handles **<=16 drift**.
The exact frozen soak checkpoint, warmup, window/median and workload definitions
remain authoritative; do not substitute a replay's maximum or a diagnostic spread.

A compliant implementation must expose and verify an explicit **backing ownership
and release capability** for Project-sized private storage:

| Backing class | Required physical boundary | Proof obligation |
|---|---|---|
| Construction private backing | Operation completion, including failed PUBLISH/LOAD/BUILD/REBUILD | Live allocations and borrowers zero, workers finished, backing release completed; no Project-sized commit retained in an uncontrolled general Server heap |
| Resident private backing | UNLOAD | All resident allocations destroyed, borrowers detached, mappings/handles reset, owned backing released |
| Server/connection backing | Survives UNLOAD | Declared bounded retention independent of past Project size/count; included in aggregate process budget |
| File/SHM mappings | Their respective logical owner boundary | This process's views/handles closed; mapping region and private commit measured separately |
| OS/CRT/Unknown residual | No assumed exemption | Accounted independently and bounded within the same aggregate gate; unexplained residual is OPEN, not success |

Here “backing release” is a design capability, **not an existing method or chosen
allocator**. It must physically decommit/release exclusively owned pages/regions
through a lifetime-bound owner, with observable completion. clear(), destructor
counts and deallocation into a shared CRT heap alone do not prove that capability.
The current code has no established Project-domain coverage/release mechanism
which guarantees this irrespective of the shared NT heap's decommit timing.

The implementation design must enumerate all sources, including helper locals,
containers' nested allocations, worker-produced blocks, filesystem strings,
diagnostics and error paths. The measured Construction subset (35.90% of captured
Alloc count / 48.05% of byte traffic) is **not sufficient allocation coverage** for
a complete domain design. Unknown cannot be automatically routed or reclassified.

Ordinary std::vector/std::string allocations do not migrate merely by creating
a domain object. Any later explicit allocator-aware ownership strategy needs its
own reviewed integration, transfer rules, thread-safe deallocation and unwinding.
No global new/delete override, TLS allocator switching, private heap adoption,
forced heap trim, EmptyWorkingSet or server restart is authorized by this review.

### Budget accounting and limits of a guarantee

The process gate is aggregate: Server retention + bounded connection/request
buffers + owned-domain residue + CRT/OS/other residual must fit the **same**
128 MiB drift, not 128 MiB per category. Sub-budgets are unassigned until measured;
this document does not invent headroom. Reserved address space is not committed
memory, but Project-owned reservations/handles still require lifecycle cleanup.
Relabeling retained Project storage as a Server cache is not release.

The checkpoint assumes the agreed workload, client count and settled request/
outbound state. Arbitrary unrelated new clients or undrained user messages may
allocate Server/TCP memory; UNLOAD cannot guarantee an absolute whole-process cap
under unbounded unrelated activity. That does not excuse Project retention or
change the existing release gate. Preserve active healthy TCP connections.

## 6. Required transition sequence and failure semantics

PUBLISH/LOAD success: finish construction workers → finish final images/runtime →
detach resident storage → retire Construction borrowers/objects → release its
physical backing → return with Resident state only (plus legitimate Server state).
Actual resident publication may precede final local destruction today; no borrower
may cross that overlap in either ordering.

UNLOAD success: ensure legal/quiescent runtime state → retire Project publishers
and borrowers → destroy Resident state/close mappings → complete backing release →
report UNLOADED. GET_STATE then LOAD/PUBLISH must work on the **same** connection.
No code change to current UNLOAD is prescribed before a concrete mechanism/defect
is selected and reviewed.

Failure before resident publication: unwind workers/temporary state and partial
maps, publish no resident object, and perform the same physical release obligations.
Failure of an invalid-state UNLOAD does not destroy the active Project. A release
failure must not be reported as proven memory-budget success; reporting/recovery
behavior remains an explicit design item. Existing BUILD persisted-lineage mutation
semantics are unaffected; this is not a disk rollback contract.

## 7. Verification matrix before any release candidate

| Obligation | Existing evidence | Still needed |
|---|---|---|
| Normal logical lifetimes | RAII/owner source review; LIFETIME-03 captured construction/resident frees | Exhaustive allocation/borrower inventory, direct-VA and pre-trace/Unknown coverage |
| Worker quiescence | Synchronous lanes/jthread scopes | Failed startup, event/wait failures, exception/failure unwinding coverage |
| Project subscriptions | No active production caller; generic registration cleanup observed | Scope-specific detach contract and stale-callback/connection-close ordering tests before adding publishers |
| Physical budget | Real server OFF/USE FAIL; UMDH/ETW/CDB attribution | Selected explicit backing mechanism + full-size server gate, not standalone replay |
| Same TCP lifetime | Prior lifecycle checks PASS | Repeat for each actual candidate and failures; no reconnect/reset workaround |
| Semantics/parity | Previous accepted V2/PGO reference artifacts | Candidate PUBLISH/LOAD/BUILD/REBUILD, values/links/WHO/WHERE/SourceMap, runtime plans and native SHM checks |
| Release performance/PGO | Frozen measured references retained | Any actual source change needs its own performance/regression gate and matching newly trained PGD |

Construction logical release and physical release require separate instrumentation:
owner live/reserved/committed accounting and barrier completion, external process
PrivateUsage/handles, and actual VirtualQueryEx mapping classification. A single
metric cannot substitute for the others; controlled diagnostics may supplement
but must not redefine the frozen release checkpoint.

## 8. Decision record

01–03 established observed deallocation/decommit and heap-domain attribution,
with ownership coverage limits. 04–06 did not reproduce the server's failure in
shared replay. 07–11 established control/observer/build variability, not a reliable
production remedy. Their reports remain evidence, not authorization to continue
replay indefinitely or select a new allocator.

CONTRACT-12 deliverable is the ownership/borrow/barrier/physical-capability contract
above. **Contract definition COMPLETE; physical mechanism/design integration OPEN;
exhaustive ownership OPEN; memory release gate FAIL.** The next implementation
proposal must show explicit allocation coverage and page-release proof on the real
lifecycle before coding/adoption. No implementation was selected or performed.

No production edits, builds, benchmarks, new replay, commit, push or deployment
in CONTRACT-12. Existing uncommitted work is preserved. Frozen OFF/USE/final PGD
remain unchanged. PGO OFF / NO ADOPTION / RELEASE BLOCKED.
