# PROJECT-UNLOAD-REPLAY-FIDELITY-05

2026-10-29. **Read-only fidelity profile COMPLETE / replay reproduction OPEN /
NO ADOPTION / release BLOCKED.** No server, shared/private replay, UMDH, CDB or
new ETW capture was run. Production C++, allocator, UNLOAD and PGO are unchanged.

The ZIP mentioned in the request was not attached or available locally. Local
exporter and streaming auditor implement the specified read-only investigation.

## Export and quality checks

The existing PID **19288** trace was exported directly to
`build/project-unload-fidelity-05/events-audit.jsonl` (2539935412 bytes), retaining
raw Alloc/Free SourceID, TID, heap, address, allocation size and UTC ticks. Thread
start/stop events are separately preserved in `threads.jsonl`. External lifecycle
boundaries retain their original timestamps. Unknown owners remain Unknown.

Every Alloc/Free event is cross-checked against the existing DOMAIN-04 normalized
stream for heap/address, TID, size, time, operation and order; owner labels are
carried forward from that conservative subset. This is not an attempt to infer
SourceID from the previous binary format, which did not store it.

Quality gates passed:

- PID 19288, captured creation FILETIME **134376721967437667** agree across target,
  recorder readiness and harness status.
- Unchanged diagnostic executable SHA-256
  `dac808b7dd4f5a72e8eec7ce5e6f8172461d5b3cf2612c38fa15f0e53b50af0d`.
- Trace module/private PDB GUID **b99e77e4-59f3-434a-abf9-39dc6920444c**, age 1,
  checked; private PDB loaded for symbol resolution.
- Zero ETW events lost, trace not truncated.
- Alloc **9536484**, Free **9536630**, matched Free **9536471**,
  unmatched Free **159**, final captured live blocks **13**.

Identity limitation: creation time comes from the original recorder's validated
process identity and archived manifests, not an independently reconstructed
process-start event. Full executable SHA comes from archived identity and the
unchanged local image, not a cryptographic image hash embedded in ETL.

## Allocator paths: original workload

Raw SourceID is retained. Microsoft documents 1 lookaside, 2 LFH, 3 main path,
4 slow path, 5 invalid and 6 reserved segment value. No unobserved value is guessed.

| Raw SourceID | Alloc | Alloc % | Free |
|---|---:|---:|---:|
| 2 / LFH | 9455792 | 99.15386 | 9455882 |
| 3 / main path | 80692 | 0.84614 | 80748 |

All matched LFH allocations freed through SourceID 2 (9455782 pairs), and all
matched main-path allocations freed through SourceID 3 (80689 pairs). The extra
Free counts include the 159 unmatched blocks; they are not assigned fictional
allocation origins.

**No original-vs-replay path discrepancy has yet been measured:** DOMAIN-04 did
not capture its own SourceID events. Enabling LFH on both heaps does not establish
equal path distribution. Do not select a path correction based solely on these
original-only percentages.

## Threads and cross-thread frees

| Metric | Result |
|---|---:|
| Matched Free on a different thread lifetime | **210778** |
| Share of matched Free count | **2.21023%** |
| Cross-thread freed AllocSize bytes | **5041990431** |
| Share of matched freed bytes | **31.75039%** |
| Captured Thread/Start | 8776 |
| Captured Thread/Stop | 8781 |
| Distinct raw TIDs in Alloc/Free | 648 |
| Maximum captured start-minus-stop balance | +24 |
| Minimum captured balance | -5 |

Raw TIDs are reused. The auditor resolves each allocation/free to the most recent
captured start for its TID and compares **(TID, generation)**, rather than assuming
each raw TID represents one immortal thread. Starts before attachment use an
explicit initial/unknown generation. The +24 balance is not an exact absolute
thread peak: the initial thread population is not captured, and five more stops
than starts were observed. It is also not proof of 24 simultaneously executing
allocations or concurrent execution at CPU-instruction granularity.

Cross-thread Free counts by allocation origin: Unknown **151434**, Construction
subset **52301**, Resident origin **5888**, Server origin **1155**. By allocation
path: LFH **197233**, main path **13545**.

This is a substantial byte fraction missing from a one-thread replay. A next
shared-only fidelity test should preserve thread lifetimes and allocate/free
thread identity while respecting the original dependency order. No such replay
was implemented in this phase.

## Sizes, heaps and captured live state

- Captured size range: **1–39493311 bytes**.
- Cumulative event AllocSize: **15880094838 bytes**.
- Peak captured live bytes: **596919014 bytes / 569.26633 MiB**.
- Peak captured live block count: **36903** (not necessarily the same instant as
  peak live bytes).
- Heap 2415652175872: **9527709 Alloc**; heap 2415656566784: **8775 Alloc**.
  Both heap identities are retained, without assigning an owner to the second
  heap from its correlation with thread count.

Full exact-size histogram, power-of-two buckets, owner totals, phase counters,
path-pair counts and all 274 boundary live snapshots are in `profile.json`.
Common sizes include 32 (832589 events), 24 (753597), 48 (745147), 70 (323804)
and 166 bytes (285048). Non-aligned event sizes alone do not prove a replay size
bug. Microsoft describes Size as bytes allocated from the heap; the trace does
not independently supply the original C++ request plus CRT adjustments, so no
size correction is justified yet.

At cycles 70/80/90, P_after has **22 blocks / 4115308 bytes**, and both post-UNLOAD
checkpoints have **17 blocks / 5779 bytes**, matching LIFETIME-OWNERS-03. Startup
allocations before trace attachment remain outside captured live totals.

Phase labels use external checkpoints, not instrumented entry into every server
function: after UNLOAD2 to the next P_after approximates PUBLISH; RUN/FREEZE and
UNLOAD1 are grouped; LOAD and UNLOAD2 are grouped. Final shutdown can fall in the
last window, so phase counters must not be interpreted as exclusive function
attribution.

Initial heap fidelity remains unresolved. DOMAIN-04 precommitted a **152583760-byte**
metadata table before its baseline. Subtracting baseline removes its constant
Private Bytes from drift, but does not make startup allocation/address/heap
state identical. Neither original pre-attach heap contents nor replay allocator
path distribution is reconstructed by this profile.

## Validation and decision

Seven contract tests PASS: same-thread pairing, cross-thread pairing, duplicate
live key rejection, unmatched Free, event-order rejection, Unknown preservation,
and identity mismatch rejection. All production count gates pass on the full
19-million-event stream. Python syntax checks pass.

The concrete fidelity gap established here is **thread lifetime / cross-thread
Free**, not a demonstrated SourceID or request-size mismatch. Continue only with
a separate shared-only thread-lifetime fidelity experiment if authorized; measure
its allocator paths and retention. Until shared reproduces >128 MiB drift, do not
accept a private domain. No global allocator override, TLS switch, heap trim,
commit, push, deployment or production-source change was performed. PGO OFF;
release remains BLOCKED.

Evidence: `build/project-unload-fidelity-05/profile.json`, `quality.json`,
`events-audit.jsonl`, `threads.jsonl`, `symbols.log`, build/export/audit logs.
Sources: `experiments/project-unload-fidelity-05/`.

References: [Microsoft ETW heap allocation Source/Size](https://learn.microsoft.com/en-us/windows/win32/memory/etw-heap-event-alloc),
[Microsoft ETW heap Free Source](https://learn.microsoft.com/en-us/windows/win32/memory/etw-heap-event-free).
