# PLS10-C7 — Already-Typed Endpoint View Fast Path

One fast path over C5. Graph/slot APIs, double new-binding lookup, queue,
persistence, temporal Source semantics and production V2 dispatch are unchanged.

```powershell
cmake -S experiments/parallel-local-source-10-c7 -B build/parallel-local-source-10-c7 -G "Visual Studio 18 2026" -A x64
cmake --build build/parallel-local-source-10-c7 --config Release --parallel 4 --target PublishTypedView10C7 TypedViewAudit10C7
ctest --test-dir build/parallel-local-source-10-c7 -C Release -R '^PLS10C7_' --output-on-failure
python experiments/parallel-local-source-10-c7/run.py build/parallel-local-source-10-c7/NEW-OUTPUT
```

Optional `PLS10_CORE_LIBRARY` reuses a matching prebuilt PLS08Core, as in C5/C6.
It was used locally with identical MSVC Release + IPO and four Source flags.
The default builds the core normally. C5's existing native executable and the
historical private UnitProXL fixture are also required by the runner.

`PLS10_C7_FAST=0` retains legacy resolution, `=1` enables the view. For a path
with no dereference, no required final-reference follow and valid domain proof,
the callback receives existing WHO/root type/value type and a short-lived span
over the typed path. It does not copy the path into an owning target or step
through types again. All other cases use the original temporal resolver.

Domain proof is one safety bit on existing per-event scratch, computed during
the first member resolution. Union or inherited-member paths conservatively
fall back, preserving union/virtual-base guards and first-diagnostic priority.
The shared PLS02 endpoint layout is unchanged: a private checked_endpoint
wrapper annotates only C7 event scratch. There is no new model, index, cache or
project-wide buffer. The proof bit is propagated to expanded string elements.

View lifetime is callback-only. Producer scratch is not changed while a view is
consumed. Graph's existing path interner copies/owns the encoded data before the
callback returns. For fallback, the owning temporary stays alive across the
callback; nested binding callbacks keep both source/target spans valid. No view
is stored in Graph, SourceMap, pending state or across an event boundary.

Native executable has no microtimers or hot coverage counters: counting and
full legacy comparison are compiled into TypedViewAudit10C7 only. Cold audit
resolves every eligible endpoint using the legacy algorithm and compares WHO,
types and every step before consuming the view. Audit time is not benchmark data.

384 native fixture runs cover off/fast/audit, 1/2/4 workers, reverse dispatch,
exact diagnostics/warnings, compiled/runtime digests and independent SHM values/
pointers. Focused cases include strings across repeated buffer growth (40x128),
union/nested union/string paths, unbound-before-union priority, reference-copy
current binding, volatile roots, const rejection and unsupported virtual bases.

The runner freezes C5/native/audit binaries, performs cold full object-storage
checks, then uses 15 rotating four-arm blocks (C5, internal off, fast, V2).
Artifact SHA checks are outside native PUBLISH timing. The same-contract images
must match C5; V2 is a separate image domain. Native off/fast counters must be zero.
No builds/tests should run concurrently with timing.

The measured result and decision are in
`docs/PLS10_C7_ALREADY_TYPED_ENDPOINT_VIEW.md`. No arbitrary future visibility,
new Source grammar or production adoption is claimed by this experiment.
