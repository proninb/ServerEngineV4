# PLS07-A — Final Result Ownership Transfer

No compact format, filtering, new workers, materialization or production change.
PLS03 flat interning, PLS04 early local release and PLS06 dense heads stay enabled.

```powershell
cmake -S experiments/parallel-local-source-07 -B build/parallel-local-source-07
cmake --build build/parallel-local-source-07 --config Release --parallel 8 --target PLS07
ctest --test-dir build/parallel-local-source-07 -C Release --output-on-failure
python experiments/parallel-local-source-07/run.py build/parallel-local-source-07/measurements --pairs 15
python experiments/parallel-local-source-07/summarize.py build/parallel-local-source-07/measurements
```

`PLS07_OWN=0/1` selects dependent baseline/owning result in one executable.
`PLS07_VERIFY=1` enables expensive ownership correctness checks, excluded from
lifecycle and disabled for timing pairs. `PLS07_OWNER_PATH` writes ownership
metadata. Existing PLS04 memory and PLS03 coarse-profile paths remain supported.

Candidate releases bound endpoints after the last write-target canonicalization
and before LWW. After alias scratch destruction, eight buffers are moved to
`owned_result`: paths, full write/binding rows, declaration origins, canonical
cells, final values/bindings/warnings. Exact data-pointer identity is checked.
Then bound storage is cleared before consumer construction/readout.

`consumer_view` exposes paths, values, events, origins and declaration records.
Write/binding views do not expose their old endpoint ordinals as live handles.
Those fields remain in private unmodified archive rows; no losing rows are
deleted, deduplicated or re-resolved. Final result links use stable cell IDs and
indices into the owner's records. Views borrow the owner and must not outlive it.

This is independent ownership of current semantic output, not a standalone
project/type/name database or a replayable incremental BUILD program. Existing
WHO/type/file-ID domains and immutable catalogs remain external. Original typed
endpoint syntax for arbitrary losing events is not reconstructible from public
views alone: incremental replay may require deterministic Source replay.
No incremental persistence contract is introduced in this experiment.

Normal target is Release+IPO. Audit main uses `/GL-` for the known MSVC global
allocation-observer LTCG ICE. Audit counts/payload are separate from normal
timing/RSS. Full lifecycle includes endpoint destruction, ownership transfer,
consumer fingerprint and eventual owner destruction. Oracle/verification,
profile/report I/O and Header/lexical preparation are excluded. This is not
production PUBLISH or Direct G.

The runner needs existing UnitProXL/oracle and frozen PLS06 at its configured
paths. It checks both executables/modes/orders, history and semantic hashes,
buffer identity, empty bound storage, allocation payload and retained capacity,
then performs five calibration pairs and 15 alternating A/B pairs. Use a fresh
output directory.
