# PLS10-C2 — Slot-Based Direct Commit

2026-10-25. Result: **PASS at the transient API / typed-input kernel boundary**.
Full Source integration and UnitProXL PUBLISH A/B are deliberately not part of C2.

## Change

Added `graph::apply_resolved_initialization()` with a plain noexcept callback
and a transient `initialization_apply_result {slot, existed, applied}`.
It uses the existing initialization target index, calls the producer decision
before replacement, and returns a zero-based initialization slot. The producer
owns Source policy; Graph has no Source order, warning or PLS fields.

The previous `add_resolved_initialization()`, its upsert implementation and
production V2 dispatch are unchanged. No data members were added to Graph,
`object_initialization_record` remains 24 bytes, and persistence layouts/code
were not changed. The new result is an API return object, not a stored record.

For an existing target, accept updates only value; reject leaves it unchanged
and still returns its slot. For a new target, Graph prepares index/vector
capacity before the callback so no allocation is needed after successful policy.
Rejection appends no record, although reserved capacity may increase. Callback
errors propagate with cleared output and no Graph record mutation. The callback
must not mutate/reenter Graph and must handle its own scratch allocation failure.

## Producer

`experiments/parallel-local-source-10-c2/slot_commit.hpp` replaces C1's
endpoint-to-metadata map with `metadata_by_slot`. Losing events store slot and
provenance only. No direct values, endpoint index or direct-winner preparation
are retained. A typed operation with a ready target calls apply immediately;
the same operation can wait for a final-target resolver and then call that apply.
The A2 binding algorithm has not been replaced or modified.

Both immediate and delayed calls use one normalization function before Graph
lookup. In particular, a one-member path-wrapper becomes the existing direct
member encoding. General final-target alias resolution still belongs to the
semantic producer, and union overlap remains outside the supported kernel.

Slots refer to `initialization_entries()[slot]` and survive append, value
replacement, vector relocation and index rehash. They do not survive reset,
removal/reordering or rebuild. Seed slots lacking Source metadata do not count
as previous Source events. All writes to a tracked cell must use this producer
while its sidecar is alive; this is not a concurrent mutation API.

## Verification

MSVC Visual Studio 2026, Release + IPO:

- C2 CTest: **1/1 PASS**, including inherited C1 scenarios with the slot producer.
- All 24 arrival permutations preserve winner and adjacent warning sequence.
- Late order 2 after orders 1 and 3 leaves value 3 and warnings 1->2->3.
- Final rebinding sends a waiting operation to the final reference target.
- SourceMap retains losing contributor roots/files; exact warning offsets remain
  in temporary provenance. Header defaults do not generate Source duplicates.
- Direct-member and equivalent path-wrapper yield one slot and one Graph record.
- 8192 new targets followed by reverse revisits preserve every saved slot across
  repeated index rehash/vector growth. Earlier slots and seed gaps remain valid.
- Reject-new, reject-existing, callback error, invalid input and null callback
  behave as specified without changing existing Graph values.
- An always-accept callback produces the same final initialization records as
  the original API on the same operation sequence.
- Existing `compiled_project`, `source_map`, `frontend_regressions`: **3/3 PASS**
  after rebuilding against the modified Graph header/implementation.
- Python generator compile and `git diff --check`: PASS.

Logs: `build/parallel-local-source-10-c2-{configure,build,tests}.log`,
`build/parallel-local-source-10-c2-regression-build.log`,
`build/parallel-local-source-10-c2-regressions.log`.

## Work eliminated, and limits of this result

At this kernel boundary there is one Graph target lookup per submission and no
producer endpoint lookup. Insertions still require index placement/possible
rehash; "one lookup" does not mean one hash-table probe. Source metadata is a
dense indexed read inside the callback. In the 1/3/delayed-2 fixture there are
three decisions and two value mutations, with no second direct-values pass.
Diagnostic projection visits metadata only and does not replay values into G.

These are structural/count checks, **not CPU timing or a PUBLISH speedup**.
Callback dispatch, dense metadata allocation, normalization and diagnostic
projection still cost work. No extrapolation from C1/B timing is justified.

Next step is integration with the real typed Source producer and unchanged A2
final bindings, preserving declaration/const construction, supported-domain
guards, deterministic diagnostics and native SourceMap. Only after the complete
candidate passes independent semantic/runtime oracles should UnitProXL full
PUBLISH be compared with A2 and production V2. C2 does not claim that integration,
full Header/Source parity or completion of PLS10-C.

Reproduction: `experiments/parallel-local-source-10-c2/README.md`.
