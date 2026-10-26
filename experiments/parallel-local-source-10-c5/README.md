# PLS10-C5 — Streaming Project Visibility / Forward-Use Audit

Question: does the original UnitProXL corpus actually require Source objects
from later declarations? This experiment removes C3's per-file declaration
restriction, retaining its bounded parallel parsing and ordered direct commit.
It does **not** pre-register future objects or retain all parsed file models.

```powershell
cmake -S experiments/parallel-local-source-10-c5 -B build/parallel-local-source-10-c5 -G "Visual Studio 18 2026" -A x64
cmake --build build/parallel-local-source-10-c5 --config Release --parallel 4 --target PublishStreamingSource10C5
ctest --test-dir build/parallel-local-source-10-c5 -C Release -R '^PLS10C5_' --output-on-failure
python experiments/parallel-local-source-10-c5/run.py build/parallel-local-source-10-c5/NEW-OUTPUT
```

Default build compiles its own core. Optional `PLS10_CORE_LIBRARY` can reuse an
already-built matching C4 PLS08Core.lib for this local audit; compiler, runtime,
IPO and the four Source feature definitions must match. The measured local
series used that option. The frozen executable contains the statically linked
core; no runtime dependency on that .lib exists.

Objects declared in prior files or earlier in the current file are visible
without extern. Every operation reads current bindings and immediately updates
G in declaration-order DFS of project.json. Worker completion order is ignored.
A fixed pool/ring bounds in-flight parsed files (window=2*workers), and consumed
models are released immediately. There is no complete-catalog barrier, second
syntax pass, all-project model retention, Alias Graph or separate Writer.

On a genuinely future or unknown object this audit path reports an error; it
does not implement waiting/rescheduling. That is a deliberate capability probe,
**not a rejection of the user's project-wide forward-visibility language rule**.
C4 separately demonstrates general forward visibility through a complete
catalog. A future wait/spill/discovery design must solve bounded-queue deadlock,
end-of-discovery detection, type conflicts and deterministic WHO/diagnostics
before it can replace that prototype for arbitrary inputs.

96 native fixture checks pass across 1/2/4 requested workers and reverse dispatch.
They verify project-visible prior declarations, temporal binding, exact origins,
native SHM values/pointers, artifact determinism and bounded queue wrap. Dedicated
fixtures prove that this path detects an actual future declaration and an unknown
name; therefore successful UnitProXL execution tests absence of such a dependency
on its executed supported-grammar paths.

Original, unmodified UnitProXL preflight passes for both 1 and 4 workers, all
3373 Source files. compiled.bin/runtime.bin match across worker counts. Full
2,116,048,327-byte object-storage SHA matches V2:
`cb5aa2607c10f928b273ad8f5d33c29f3656190b3ec9597550cbf068a5f0d0e2`.
Thus a complete Source declaration catalog is **not required for this corpus**.
Prior failures in C3 were caused by its stricter per-file extern rule.

Input acquisition/token storage and G themselves are not bounded by the ring;
the ring bounds parsed-file models only, not the byte size of one large file.
Current bindings are queried through the existing Graph index; no new endpoint
or binding index is introduced. Shared origin metadata is dense by stable slot.

The full timing series and adoption decision are recorded in
`docs/PLS10_C4_C5_PROJECT_VISIBILITY.md`. Production V2 and BUILD remain unchanged.
