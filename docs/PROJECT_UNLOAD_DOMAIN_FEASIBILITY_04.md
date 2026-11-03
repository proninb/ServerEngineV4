# PROJECT-UNLOAD-DOMAIN-FEASIBILITY-04

2026-10-29. **INCONCLUSIVE / NO ADOPTION. Release remains BLOCKED.**

Standalone MSVC replay and offline normalization ran successfully. The shared
control did not reproduce the source process's >128 MiB quiescent drift. The
private arm's smaller footprint is therefore not evidence sufficient to adopt
an allocation domain in ServerEngineV4.

## Input and classification

Input is the existing LIFETIME-OWNERS-03 PID 19288 trace, not a new capture.
Zero lost events, not truncated, matching private PDB GUID/age are checked before
export. The user-mentioned ZIP was not attached or found locally; the local
exporter/replay/comparator implements this bounded diagnostic experiment.

| Conservative classification | Allocations | Allocation count % | Sum of event AllocSize bytes | Byte % |
|---|---:|---:|---:|---:|
| Construction subset | 3423321 | 35.8971 | 7630472652 | 48.0505 |
| Resident origin | 8118 | 0.0851 | 153714068 | 0.9680 |
| Server origin | 11388 | 0.1194 | 680853 | 0.0043 |
| Unknown | 6093657 | 63.8984 | 8095227265 | 50.9772 |

Byte totals are cumulative allocation traffic, not peak memory. Construction is
restricted to reviewed local-context stack families (graph, string_table,
identity_space, manifest_composer, lexical_generation and file_context), during
a PUBLISH interval. Resident factory/binding ancestry takes priority. The
classification is a conservative operational subset, not exhaustive proof of
all C++ owners. Its live count must be zero at **every** captured P_after or
normalization fails. Other allocations remain on their source heap in both arms.

9536484 Alloc and 9536471 matched Free are replayed; 159 unmatched Free events
are preserved as excluded records, not converted to fictitious allocations.
13 captured blocks remain at trace end. No Realloc events occur; an unknown
Realloc schema is rejected. Keys are (original HeapHandle, original address),
mapped to monotonically increasing allocation IDs. Allocation thread, UTC ticks,
size, owner and original heap/address are retained. All 274 external boundaries
are inserted into timestamp order, including P_after and both UNLOAD checkpoints.
The previous after_unload_2 checkpoint can serve as P_before for the following
cycle; it is an approximation, not a newly measured exact PUBLISH-entry timestamp.

Each allocation has a separate stack-ID sidecar entry. The auditable re-export
is byte-identical to the stream used for replay: SHA-256
`310acca34f2f1fe51fec82ce8dc2ee9a7f727cc60eb4af8871086df86406db62`.

## Replay mechanics and checks

Built with MSVC /O2 /MD as an independent Windows console executable. It neither
links ServerEngineV4 nor overrides new/delete. Shared maps the first observed heap
to GetProcessHeap and additional original heaps to distinct HeapCreate handles.
Both arms enable LFH consistently. Private routes only owner=Construction_subset
to a separate growable heap; Resident/Server/Unknown retain the shared mapping.

At every P_after, HeapDestroy is permitted only after verifying zero live
Construction blocks. It destroys only the experimental domain, never the process
heap; the next classified allocation creates a fresh domain. All matched Free
operations validate the allocation ID and recorded owner. Input/boundary counts
and allocation/free totals are checked against normalization.

Replay metadata uses a separately VirtualAlloc-backed 152583760-byte pointer table,
initialized before the initial Private Bytes sample. The table is identical in
both arms; reported drift is relative to each process's initial sample. Boundary
Private Bytes before and after domain destruction are both retained.

The negative contract test allocates a Construction block and reaches P_after
without freeing it: replay exits with the required live-Construction failure.
This demonstrates that the domain is not being destroyed to hide live ownership.

## Results

| Metric | Shared | Private subset |
|---|---:|---:|
| Maximum post-first-UNLOAD drift, MiB | 64.34375 | 19.03125 |
| cycle 70 post-UNLOAD drift, MiB | 64.34375 | 6.734375 |
| cycle 80 post-UNLOAD drift, MiB | 12.699219 | 16.800781 |
| cycle 90 post-UNLOAD drift, MiB | 59.667969 | 2.945313 |

The source process's absolute Private Bytes were 187.332031 / 20.109375 /
185.804688 MiB at cycles 70/80/90 (initial 1.210938 MiB). Shared reproduces an
attenuated high/low shape but not the original failing drift. Private is not even
lower at every selected checkpoint. These are one run per arm, a mechanism
screen rather than a performance/statistical experiment. No acceptance claim is made.

## Fidelity limits and decision

The replay exactly preserves the exported captured allocation/free order and event
AllocSize values, but is **not an exact reproduction of the original heap workload**:

- Original concurrent worker lifetimes, heap interleaving, affinity and timing are
  replaced by one executing thread. TIDs remain metadata.
- Startup heap state/allocations before ETW attachment and unmatched allocations
  cannot be reconstructed from this trace.
- Provider AllocSize is not independently verified as the underlying CRT request
  size. Allocation contents are not reproduced.
- Construction classification covers 35.90% of allocations and 48.05% of byte
  traffic; Unknown is not promoted to improve the private result.

Thus the required shared reproduction gate is not met. Next work, if authorized,
must first investigate concurrency, exact CRT request sizing and startup heap
state fidelity. Do not move to a production or diagnostic-source allocator
candidate on this result. No global new/delete override, TLS allocator switch,
heap trim, UNLOAD change, production rebuild, PGO training or deployment occurred.
PGO default remains OFF.

## Evidence

Requested machine-readable result:
`build/project-unload-domain-04/comparison.json`.
Other local evidence: `normalization.json`, `normalized/classification-stacks.json`,
`normalized/allocation-stack-ids.bin`, `events.bin`, `boundaries.json`,
`shared.jsonl`, `private.jsonl`, `export-identity.json`, compile/export logs.
Source: `experiments/project-unload-domain-04/`.

Microsoft documents that [HeapDestroy releases the private heap's committed pages](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapdestroy).
That API property does not establish that isolating this subset solves the server's workload.
