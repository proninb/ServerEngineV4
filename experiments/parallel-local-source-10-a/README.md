# PLS10-A — Final Binding Reduction

Only step A is implemented. A temporary dense winner table replaces node-based
binding catalogs and their full copies; dependent targets use sparse context
deltas. Direct targets require no stabilization round. G and its ABI are
unchanged. No persistent operation history, new WHO/type database, new Semantic
Linker, workers, compact ABI, Runtime or BUILD changes are introduced.

The existing typed-input adapter and value/warning/publication stages remain as
an independent comparison harness. This is **not** completion of PLS10-B/C or a
claim that bound_model/Alias/Writer have already disappeared from the whole path.
The reducer itself reads the const Header/object G and returns final bindings;
the unchanged harness later writes G. Existing extraction remains sequential in
the measured harness; its local-task format is reused, not a new worker pool.

```powershell
cmake -S experiments/parallel-local-source-10-a -B build/parallel-local-source-10-a
cmake --build build/parallel-local-source-10-a --config Release --target PublishFinalBinding10A FinalBindingAudit10A FinalBindingSemantic10A BindingReductionTests10A --parallel 8
ctest --test-dir build/parallel-local-source-10-a -C Release -R '^PLS10_' --output-on-failure
python experiments/parallel-local-source-10-a/run.py build/parallel-local-source-10-a/ab-new --pairs 31 --v2-pairs 15 --final-mode 2
```

`PLS10_FINALIZE=0/1/2` selects B1 / A1 / A2 in the same executable, outside alias
evaluation. A1 replaces catalogs/copies; A2 additionally skips duplicate-target
canonicalization and diagnostic heads when direct-only bindings provably have
one unique target per operation. Conflicting/nested cases retain the old warning
projection. Use `--final-mode 1` to reproduce A1 separately. B1 control uses
`PLS09_INDEXED=1`. Production V2 control uses
`PLS08_CANDIDATE=0`. The runner freezes binaries and private input, keeps deep
validation/SHA outside timing, verifies same-contract persisted image digests and
uses actual native Runtime/resident SHM. Original projects are untouched.

The canonical interner and reference cache are shared with the B1 control; no
second syntax parse or type catalog is built. Source visibility is the existing
typed-input adapter's contract, not a new frontend policy implemented by A.
Per-file TYPE/extern validation remains an input gate for the eventual complete
PLS10 path; A does not claim to replace object name/visibility resolution.

## Dependent targets are not an unconditional one-pass reduction

For `struct N { N& next; };`, declarations `N a; N b;` and:

```cpp
a.next = a;
b.next = a;
b.next.next = b;
```

two maps satisfy a purely simultaneous final-target equation. The established
direct-seed policy chooses `a.next -> b`, `b.next -> a`. It must not silently
switch to the other solution. With `a.next=a; b.next=b; a.next.next=b;` the target
oscillates and the existing nonconvergence diagnostic must remain.

A keeps that policy: all nested targets resolve against the previous context;
only afterwards is the next sparse delta committed. Contributions that move
away restore the original direct winner. Caught unresolved errors invalidate
reference cache/visiting at the same boundaries as B1. No full direct catalog is
copied into each next context. The same bound and first-error policy are kept.

`binding_reduction.hpp` has no Graph dependency: it stores only winning operation
IDs and sparse per-context state. The direct table and cell list are dense, and
overlay allocation is absent if there are no indirect binding targets. This is
not a transport model of all writes/endpoints/origins under a new name.

The original typed operations and canonical cell IDs are kept in A to make
fingerprints, warnings, failures, SourceMap and native result independently
comparable. Sparse indirect writes and direct native commits are B/C work.

Report: [PLS10-A](../../docs/PLS10_A_FINAL_BINDING_REDUCTION.md).
