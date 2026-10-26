# PLS10-C4 — Project Visibility / Complete Catalog Prototype

Supersedes C3's file-local TYPE/extern rule. Objects are project-visible;
registration records WHO/type only. Initializers and bindings are applied later
in original Source order. Unknown objects, conflicting types, duplicate concrete
definitions and extern-only objects without a definition are errors. Repeated
consistent extern declarations are allowed. Header seed objects count as defined.

```powershell
cmake -S experiments/parallel-local-source-10-c4 -B build/parallel-local-source-10-c4 -G "Visual Studio 18 2026" -A x64
cmake --build build/parallel-local-source-10-c4 --config Release --parallel 4 --target PublishProjectSource10C4
ctest --test-dir build/parallel-local-source-10-c4 -C Release -R '^PLS10C4_' --output-on-failure
python experiments/parallel-local-source-10-c4/run.py build/parallel-local-source-10-c4/NEW-OUTPUT
```

This deliberately simple prototype retains all per-file syntax models until
the complete declaration catalog is ready, then frees each model after ordered
commit. Parsing occurs once, Source tokens are released before commit, and no
bound model/Alias Graph/Writer is created. However, the complete-catalog barrier
and retained syntax are a property of this prototype, **not an architectural
necessity**. The user's subsequent direction is to audit actual forward uses
and prefer streaming when possible; see C5.

Registration failures are deferred to their Source event so an earlier commit
error is not masked by a later duplicate declaration. Declaration initializers
do not execute during registration. Existing temporal-reference behavior,
slot metadata, normalization and supported-domain limits are inherited from C3.

34 fixtures x 4 scheduling modes = 136 native checks cover forward objects,
forward binding targets, declaration initializer order, duplicate/type conflicts,
repeated extern, namespace visibility, copying a current reference binding and
all C3 temporal/runtime oracles. Cold object-storage parity on unmodified
UnitProXL passes for 1/4 workers; artifacts match between worker counts.

Frozen full-PUBLISH series `build/parallel-local-source-10-c4/ab-01`: 15 pairs,
V2 1624.76 ms vs C4 2202.73 ms, paired regression 575.72 ms, 95% bootstrap
interval 529.80..583.57 ms, 0/15 candidate wins. No adoption. Model vector
capacities total 182,960,089 bytes (~174.48 MiB); this is not peak RSS.

No production dispatch, G ABI, persistence schema or BUILD changes.
