# PLS10-C2 — Slot-Based Direct Commit

Minimal transient Graph API plus a typed-input experiment. No Source Parser
integration or UnitProXL timing is included in this step.

```powershell
cmake -S experiments/parallel-local-source-10-c2 -B build/parallel-local-source-10-c2 -G "Visual Studio 18 2026" -A x64
cmake --build build/parallel-local-source-10-c2 --config Release --parallel 4 --target SlotCommit10C2
ctest --test-dir build/parallel-local-source-10-c2 -C Release -R '^PLS10C2_' --output-on-failure
```

Graph API: `apply_resolved_initialization(target, type, value, decision,
context, result)`. It searches the existing target index once and calls the
producer decision before changing the value. The callback takes a zero-based
slot, existence flag and acceptance output, returning a server_status. A plain
function pointer/context avoids a heap-allocated callback wrapper. G knows
nothing about Source order or warnings. The original add/upsert paths remain
unchanged and production V2 does not call the new method.

`result.existed` describes an old record; `result.applied` says whether a value
was written. An existing rejected record still returns its slot. A rejected
new record has invalid_slot and appends nothing. Callback failure leaves Graph
records unchanged. New capacity is prepared before the callback; rejection may
therefore leave larger reserved buffers. The callback must not reenter/mutate
G, and its own scratch failure handling is the producer's responsibility.

`slot_commit.hpp` uses a dense metadata_by_slot vector and sparse losing
slot/event/origin records. It has **no endpoint index and no direct-value
array**. C1 definitions are included to share the test vocabulary; the old C1
map-based committer is never instantiated by C2. Diagnostics retrieve endpoint
records by slot only in cold projection. Header-seed slots with no Source
metadata are not prior Source writes. Both immediate and delayed calls pass
through the same endpoint normalization and apply function.

Slots survive append, value replacement, vector relocation and index rehash.
They are scoped to this construction Graph and must be discarded on reset,
removal/reordering or rebuild. All writes to a tracked cell must go through the
same producer while its sidecar is alive. Concurrent callbacks are not supported.

The C1 fixture bodies are generated unchanged except for producer selection.
Additional tests cover equivalent endpoint encodings, policy rejection/error,
invalid input, old-API always-accept parity, seed gaps and 8192 reverse revisits
after repeated rehash/growth. Union overlap and general alias normalization
remain outside this kernel; final target resolution still belongs to A2.

This proves removal of the duplicate endpoint lookup at the kernel boundary,
not an end-to-end speedup. Full Source parity and PUBLISH A/B follow integration.
