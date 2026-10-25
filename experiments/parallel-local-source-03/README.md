# PARALLEL-LOCAL-SOURCE-03

Isolated alias-cost audit and one canonical-cell-index candidate. Production
Source V2, Graph API/ABI, materialization and worker scheduling are unchanged.

```powershell
cmake -S experiments/parallel-local-source-03 -B build/parallel-local-source-03
cmake --build build/parallel-local-source-03 --config Release --target PLS03 --parallel 8
ctest --test-dir build/parallel-local-source-03 -C Release --output-on-failure
python experiments/parallel-local-source-03/run.py build/parallel-local-source-03/measurements --pairs 15
```

The runner needs the existing UnitProXL fixture, its compiled oracle, and the
frozen PLS02 executable; see paths in `run.py`. It never regenerates the oracle.
Use a new output directory for each measurement series.

`PLS03_MODE=0` preserves PLS02 algorithms. `PLS03_MODE=1` changes only the
canonical-cell index to a flat table with cached hashes and exact key equality.
IDs remain assigned on first insertion. Both modes run in the same executable.

The normal executable has phase-boundary clocks, without allocation counters or
per-operation timers. The audit executable counts map API calls, cell hashes,
comparisons, traversal steps, path reuse and allocations. Rare randomized method
samples are inclusive and overlap; they are not a CPU decomposition. Audit times
must not be used as normal performance results. MSVC 14.51 crashes during LTCG
with the allocation observer, so only the audit translation unit uses `/GL-`.
The timing executable and core retain Release+IPO.

`PLS03_PROFILE_PATH` selects profile JSON. Phase allocations count requested
payload bytes, excluding allocator headers/rounding, stack, retained input and
observer storage. Peak live requested bytes are not RSS. Hash metrics count
canonical-cell hash work; other map counters count API operations, not internal
bucket probes. Path reuse includes WHO, typed path and follow mode.

Lifecycle timing starts with Header and lexical input ready and includes local
extraction, token release, linking, local release, aliases/reduction, fingerprint
consumption and bound/final-array destruction. It excludes Header/lexical setup,
oracle checks, profile analysis and report I/O. It is not production PUBLISH.

The four CTest suites run the original PLS02 fixtures for normal/audit builds
and both modes. UnitProXL runs additionally validate all origins, zero linker
token reads, forward/reverse equivalence and the independent V2 projection.

Memory ownership is audited only: the release-after-file estimate does not
change lifetimes and cannot be added to the candidate's measured benefit.

See [report](../../docs/PARALLEL_LOCAL_SOURCE_03.md).
