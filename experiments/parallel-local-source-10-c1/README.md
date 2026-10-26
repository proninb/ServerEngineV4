# PLS10-C1 — Direct Commit Feasibility

Small typed-input fixtures against the real, unchanged Graph and SourceMap.
This is not an integrated Source Parser or a full-PUBLISH benchmark.

```powershell
cmake -S experiments/parallel-local-source-10-c1 -B build/parallel-local-source-10-c1 -G "Visual Studio 18 2026" -A x64
cmake --build build/parallel-local-source-10-c1 --config Release --parallel 4
ctest --test-dir build/parallel-local-source-10-c1 -C Release --output-on-failure
```

`direct_commit.hpp` implements one `apply(final_endpoint, operation)`. Values
are written directly to G when Source order wins. Older delayed operations
record provenance without calling Graph mutation. Scratch contains the current
event/origin per endpoint and sparse losing event/origin records, never a copy
of direct values. An unresolved operation uses the same operation type and
calls the same apply after an external resolver provides its final target.
There is no replacement for the A2 binding kernel.

Important limits:

- Input must already be type/value validated and have one canonical endpoint
  encoding per storage cell. Union overlap and alternate equivalent raw path
  encodings are not resolved by this kernel.
- Public Graph APIs do not provide initialization slot or Source order. The
  fixture uses an explicit temporary endpoint-to-metadata unordered_map. This
  duplicates Graph lookup and is not advertised as a zero-cost final design.
- SourceMap preserves contributor file/root ownership; it is not an exact
  assignment-origin journal. Exact warning offsets stay in temporary metadata.
- The fixture resolver scans a tiny real Graph link table solely as an oracle;
  it must not be copied into a production hot path. Reference chains/cycles and
  binding diagnostics remain A2's responsibility.
- No Parser integration, full semantic parity, UnitProXL A/B, speedup, BUILD or
  persistence-format change is claimed. A full benchmark must wait for a real
  integrated candidate passing the existing independent oracles.

Tests cover replacement, no automatic Source ordering, alternate endpoint
encoding, delayed older writes, final rebinding, all 24 arrival permutations of
four writes, precise adjacent warning origins, contributor SourceMap and index
growth across 4097 cells. Header defaults stay outside Source duplicate counts.
