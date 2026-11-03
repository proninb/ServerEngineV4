# Read-only replay fidelity 05

See `docs/PROJECT_UNLOAD_REPLAY_FIDELITY_05.md` for results and limits.
The exporter reads the existing ETLX, raw SourceID and thread events. It checks
the previous normalized stream for operation/key/time/size identity and reuses
its conservative owner classifications. No server/replay runs occur.

JSONL fields for Alloc: op, ticks (.NET UTC ticks), tid, heap, address, size,
owner, source (raw SourceID). Free retains op/ticks/tid/heap/address/source;
size and owner are null and recovered only from a matching captured Alloc.
Boundary: op=boundary, ticks, stage, cycle. Thread events are separate in
threads.jsonl, preserving their own ordering.

Use a fresh output folder; the exporter refuses to overwrite event JSONL.
Arguments: diagnostic root, trace root, DOMAIN-04 normalized root, output root.
It requires the locally extracted matching Microsoft DIA dependency as previous
offline readers do. No COM registration is performed.

```powershell
python experiments/project-unload-fidelity-05/test_audit.py
python experiments/project-unload-fidelity-05/audit_heap_events.py --input build/project-unload-fidelity-05/events-audit.jsonl --quality build/project-unload-fidelity-05/quality.json --expected-alloc 9536484 --expected-free 9536630 --expected-unmatched 159 --expected-live 13 --output build/project-unload-fidelity-05/profile.json
```

quality.json has fixed expected PID, creation FILETIME, executable SHA and PDB
GUID/age plus trace lost/truncated gates and verified export counts. Identity
provenance is explicit: creation is from the archived validated capture identity,
not reconstructed process-start ETW. Source/Size meanings follow Microsoft docs;
no unavailable original request size is invented. The streamed live map is keyed
by original heap/address; reused TIDs are separated by captured thread starts.
