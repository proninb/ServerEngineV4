# Shared-only thread lifetime replay 06

Result: INCONCLUSIVE / NO ADOPTION; see
`docs/PROJECT_UNLOAD_THREAD_FIDELITY_06.md`.

`exporter/` takes diagnosticRoot, traceRoot, DOMAIN-04 normalizedRoot, fresh
outputRoot. It reads the source ETLX once and preserves a common `etl_ordinal`
for heap/thread events. It never sorts the independent FIDELITY-05 files by UTC.

```powershell
python experiments/project-unload-thread-06/test_normalize.py
python experiments/project-unload-thread-06/normalize_timeline.py build/project-unload-thread-06
```

The normalizer refuses to overwrite strict-replay.bin and checks production
counts. The 48-byte binary is version-specific: op uint8, owner uint8, reserved
uint16, logical-generation uint32, ID/size/heap/address uint64, UTC ticks int64.
Ops: 1 Alloc, 2 matched Free, 3 external boundary, 4 excluded unmatched Free,
5 generation start, 6 generation stop. Start/stop generations are never reused.
Original TID/generation mapping is defined by the audit timeline and input order.
Boundary ID indexes boundaries.json; stages.bin uses 1 P_after, 2 UNLOAD1,
3 UNLOAD2, 4 other. Owner labels retain DOMAIN-04 semantics; Unknown remains 0.

Build replay.cpp with MSVC x64 /O2 /MD /EHsc /std:c++20 /Zi /Zo and link
/DEBUG:FULL psapi.lib. Run separate processes:

```powershell
replay.exe strict-replay.bin stages.bin single
replay.exe strict-replay.bin stages.bin thread-affinity
```

Capture stdout to separate JSONL files, stderr separately, and check each exit
code before continuing. compare.py expects the documented build output directory
and generates comparison.json. No private heap domain or new allocator is used;
additional original source heap handles retain separate shared mappings.

Thread-affinity preserves generation lifetimes and captured heap operation order,
not actual heap concurrency. Five pre-attach threads necessarily have incomplete
history. The single control's checkpoint phase differed from DOMAIN-04, so this
run does not support a causal thread-only improvement claim.
