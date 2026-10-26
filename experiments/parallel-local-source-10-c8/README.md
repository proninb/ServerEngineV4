# PLS10-C8 — Single Binding Lookup

Isolated Graph experiment over C7 fast. No initialization-slot, endpoint-view,
Source contract, queue, ABI, persistence or production dispatch change.

```powershell
cmake -S experiments/parallel-local-source-10-c8 -B build/parallel-local-source-10-c8 -G "Visual Studio 18 2026" -A x64
cmake --build build/parallel-local-source-10-c8 --config Release --parallel 4 --target PublishSingleLookup10C8 SingleLookupAudit10C8 SingleLookupTests10C8
ctest --test-dir build/parallel-local-source-10-c8 -C Release -R '^PLS10C8_' --output-on-failure
python experiments/parallel-local-source-10-c8/run.py build/parallel-local-source-10-c8/NEW-OUTPUT
```

Optional `PLS10_CORE_LIBRARY` uses the same matching prebuilt MSVC Release/IPO
core as C7. Otherwise the original core is built. C8 compiles a generated copy
of graph.cpp into its executables; the normal engine Graph source is not edited.

`PLS10_C7_FAST=1` keeps the established C7 fast path. `PLS10_C8_SINGLE=0` keeps
the old rebind->add path, `=1` uses known-absent append after the rebind lookup
miss. The insertion tail is extracted verbatim from existing add_resolved_link
by the generator, reusing existing capacity and index insertion functions.
No table cursor is retained: ensure-capacity may rehash. The proof of absence
is valid because construction Graph mutation is exclusive and no intervening
operation inserts a target. New handles, replacement handles, count/overflow,
key validation, allocation errors and rollback follow the existing behavior.
Strict add_resolved_link itself is unchanged.

Native target has no microtimers or hot counters. SingleLookupAudit10C8 counts
lookups only during the Source backend. Cold UnitProXL audit expects 767104
lookups (32816 hits / 734288 misses) with the old path and 399960 (32816 hits /
367144 misses) with single lookup: exactly 367144 duplicate checks eliminated.
These structural counts are not a prediction of native CPU savings under IPO.

Tests repeat 768 native C7 oracle runs across strategies/scheduling and verify
artifact/diagnostic parity. Dedicated Graph tests cover strict same-source add,
strict conflicting add rejection, rebind, invalid operands and 8192 new targets
followed by reverse replacements through rehash/growth. Stable handles and a
final link-record fingerprint must match both strategies.

The runner freezes original C7/native/audit executables, checks all cold object
bytes and digests, then performs 15 rotating four-arm blocks: original C7,
internal off, single lookup, and original V2 from the C7 binary. The V2 control
does not silently receive the C8 optimization. Native counters must be zero.
Correctness scans and output SHA checks are outside PUBLISH timing. No builds
or tests should overlap the timing series.

Full measurements and adoption decision:
`docs/PLS10_C8_SINGLE_BINDING_LOOKUP.md`.
