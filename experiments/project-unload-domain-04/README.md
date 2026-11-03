# Standalone domain feasibility 04

See `docs/PROJECT_UNLOAD_DOMAIN_FEASIBILITY_04.md`: result INCONCLUSIVE / NO ADOPTION.

`exporter/` reads the existing ETLX using matching private symbols, emits ordered
48-byte little-endian records and a per-allocation stack-ID sidecar. Owners:
0 Unknown, 1 reviewed Construction subset, 2 Resident origin, 3 Server origin.
Never classify every descendant of construct_full_project as Construction.

Record: uint8 op, uint8 owner, uint16 reserved, uint32 original TID, uint64 ID,
uint64 AllocSize, uint64 original HeapHandle, uint64 original address, int64 UTC ticks.
Ops: 1 Alloc, 2 matched Free, 3 external boundary, 4 excluded unmatched Free.
For boundary records ID indexes boundaries.json. For allocation ID n, sidecar
int32 at index n-1 references classification-stacks.json.

`replay.cpp` consumes records and stages.bin (one byte per boundary: 1 P_after,
2 UNLOAD1, 3 UNLOAD2, 4 other). Pass max allocation ID and arm shared/private.
It uses VirtualAlloc for its pointer metadata; no allocator interception.
`compare.py` validates completed arms, generates comparison.json and executes a
negative live-Construction destruction test. Existing input traces are not modified.

The result describes serialized observed-event replay, not exact concurrent
server/CRT heap reproduction. Reuse the report's limitations when citing it.
