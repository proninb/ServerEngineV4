# PLS10-C6 — Ordered Commit Cost Breakdown

Observation only over C5. No Graph algorithm, Source semantics, queue, persistent
ABI or production dispatch changes. The generated diagnostic Graph translation
unit supplies the same methods as the original static core; it adds counters
and scopes only. The normal Graph source/library is untouched by C6.

```powershell
cmake -S experiments/parallel-local-source-10-c6 -B build/parallel-local-source-10-c6 -G "Visual Studio 18 2026" -A x64
cmake --build build/parallel-local-source-10-c6 --config Release --parallel 4 --target PublishCommitAudit10C6
ctest --test-dir build/parallel-local-source-10-c6 -C Release -R '^PLS10C6_' --output-on-failure
python experiments/parallel-local-source-10-c6/run.py build/parallel-local-source-10-c6/NEW-OUTPUT
python experiments/parallel-local-source-10-c6/analyze.py build/parallel-local-source-10-c6/NEW-OUTPUT
```

As with C5, an optional `PLS10_CORE_LIBRARY` reuses a matching MSVC/IPO/Source
configuration of PLS08Core.lib; this was used locally. Without it the core is
built normally. The runner also needs the unmodified C5 native executable and
the historical private UnitProXL fixture in their existing build directories.

`PLS10_C6`: 0=off; 1=counts only; **3=independent sampling**. Mode 2 retains the
initial forced-descendant observer for forensic reproducibility and must not
be used for ranking: it inflated sampled call estimates. `PLS10_C6_SEED` changes
the deterministic sparse sample (1/1024 per scope). `PLS10_C6_REPORT` selects
JSON output, written after the native PUBLISH timer has stopped. Cold SHA/query
checks and reporting never enter the measured PUBLISH interval.

Off, counts and sampling share an executable. A separate frozen C5 binary
controls for observation-site/code-layout overhead even when timing is off.
The runner uses 15 rotating four-arm blocks and 15 independent sample seeds,
records native full time/ordered stage/teardown, and checks exact artifact hashes
after every run. Four cold runs verify the full object-storage SHA. Exact
call/hit/miss counts must agree across counts/sample runs. Queue readiness is
excluded from that invariant because observer speed can change actual waiting.

Per-file wait/take, apply, root provenance and model cleanup are timed completely.
Rare Graph index rebuilds, selected vector growth and origin-array growth are
also timed completely. Frequent scopes are sampled independently; descendants
are not forcibly timed in mode 3. Exact rare descendants are excluded from
regular sample durations and added separately. Parent/child edges keep their
own counts and sample estimates, so overlapping inclusive costs are not summed
as an exhaustive partition. Exclusive values are statistical differences and
are retained even if negative. Negative/near-resolution components cannot be
ranked. The legacy raw exclusive field is unused in mode 3.

Quality checks include estimated root budget versus exact apply_file, seed
dispersion, timer quantum and full observer overhead. The final observer still
adds non-negligible time: its estimates are diagnostic wall-clock costs, not
precise production CPU costs or an optimization speedup forecast. See the report
before drawing conclusions. The first rejected observer is frozen in ab-01;
the independent observer results are in ab-independent-01.

288 native fixture runs exercise off/counts/independent modes on the same paths,
verify persisted-image parity and independently check native values/pointers.
A duplicate fixture checks exact lookup hit/miss and warning counts. Temporary
target counts indicate construction sites, **not** actual allocation calls or
bytes. No global allocator override is used in the timed executable.

Full findings: `docs/PLS10_C6_ORDERED_COMMIT_COST_BREAKDOWN.md`.
