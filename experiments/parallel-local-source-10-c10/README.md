# PLS10-C10 — reusable typed-endpoint scratch

Explicit unsandboxed recheck: V2 1353.49 ms, C10 1426.55 ms; V2 wins 15/15
pairs. See `docs/PLS10_C10_UNSANDBOXED_RECHECK_01.md`. Run the frozen-binary
`recheck_unsandboxed.py` outside sandbox with a new output directory; no rebuild
is required. The original series below did not record its sandbox launch mode.

C9 follow-up: refine allocation counts first, then test exactly one buffer
lifetime change. No Graph API/ABI, Source semantics, symbol cache, string batch,
queue, worker-count, or production dispatch changes.

The initial unchanged C7 allocation audit is frozen under
`build/parallel-local-source-10-c10/subgroup-baseline/`. Groups 1/2/3 from C9 are
refined into exclusive allocation contexts:

1. Other Source work.
2. Local extraction.
3. Remaining ordered apply_file.
4. Endpoint request construction (including its name/type lookup work).
5. String expansion outside the commit calls.
6. Scalar/link commit, including Graph/SourceMap/provenance allocations.

Nested guards restore the previous context; the partition counts each allocation
once. It measures call counts and requested payload, not function CPU time.
There are no timers on individual endpoint/Graph operations.

The candidate adds one `source_typed_endpoint` member to the private ordered
committer. `begin_endpoint()` always resets its type, object type and path length.
`save()` synchronously copies the completed path into the existing event-local
storage. Endpoint requests are nonrecursive and the member is only accessed by
the sequential commit thread, so no result retains a view into scratch and no
worker shares mutable scratch. Logical resolution is repeated as before; only
vector capacity survives between requests. No maximum depth is newly assumed.
Errors still abort ordered apply at the original decision point.

`PLS10_C10_REUSE=0` retains the original per-request temporary. Default `1`
enables scratch in this experiment only. The clean native executable is
generated directly from C7, without C9 timers/counters or replacement allocator.
The separate audit target derives from C9 and must never supply performance
measurements.

```powershell
cmake -S experiments/parallel-local-source-10-c10 -B build/parallel-local-source-10-c10 -G "Visual Studio 18 2026" -A x64 -DPLS10_CORE_LIBRARY=C:/Boris/ServerEngineV4/build/parallel-local-source-10-c4/base/Release/PLS08Core.lib
cmake --build build/parallel-local-source-10-c10 --config Release --parallel 4
python experiments/parallel-local-source-10-c10/test.py build/parallel-local-source-10-c10/Release/PublishScratch10C10.exe build/parallel-local-source-10-c10/Release/SourceAllocationAudit10C10.exe build/parallel-local-source-10-c10/fixtures
python experiments/parallel-local-source-10-c10/run.py build/parallel-local-source-10-c10/ab-01
python experiments/parallel-local-source-10-c10/audit.py build/parallel-local-source-10-c10/ab-01 0
python experiments/parallel-local-source-10-c10/audit.py build/parallel-local-source-10-c10/ab-01 1
```

Do not run builds/tests/audit concurrently with the timed runner. It freezes
all executables, uses 15 rotated/reversed four-arm blocks, verifies each semantic
domain's persisted digests after every run, and cold-checks the entire native
object storage before timing. Hashing/scans happen outside PUBLISH timing.
Summary includes both same-binary off/on and original-C7/candidate comparisons.
Do not compare absolute times against another day's series. No slow samples are
discarded. Peak WS is process-lifetime peak, not Source-only peak. Audit reports
C++ new/delete traffic, excluding pre-Source allocations and malloc/mappings.

Measured result: original C7 → reuse gains 98.77 ms paired (95% CI
81.53–141.56), 14/15 wins. Same-binary off/on gains 93.53 ms. Source allocations
fall from 2597813 to 993193, with unchanged WHO/Graph operation counts. All
816 fixture runs pass; production V2 still wins and remains unchanged.
See `docs/PLS10_C10_TYPED_ENDPOINT_SCRATCH.md` for the complete result and limits.
