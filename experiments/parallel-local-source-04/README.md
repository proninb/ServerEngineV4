# PLS04 — File-local Lifetime

Both modes retain the PLS03 flat canonical index (`PLS03_MODE=1`). Only
`PLS04_EARLY=1` releases each local model after its last semantic use; mode 0
keeps the PLS03 lifetime. Production, alias semantics and scheduling stay intact.

```powershell
cmake -S experiments/parallel-local-source-04 -B build/parallel-local-source-04
cmake --build build/parallel-local-source-04 --config Release --parallel 8 --target PLS04
ctest --test-dir build/parallel-local-source-04 -C Release --output-on-failure
python experiments/parallel-local-source-04/run.py build/parallel-local-source-04/measurements --pairs 15
```

The runner uses the existing UnitProXL fixture/oracle and frozen PLS03 timing
executable at paths specified in `run.py`. Use a fresh output directory.
It runs both modes forward/reverse against the oracle, allocation audit,
five observer calibration pairs, then 15 normal A/B pairs with alternating order.

The timing executable retains Release+IPO. Audit main uses `/GL-` to avoid the
MSVC global-allocation-observer LTCG ICE; it counts allocations throughout the
ready-input semantic lifecycle, not just aliases. Audit times/RSS include
observer allocation headers and must not be compared with normal-build timing.
PLS03 per-call/path logging is disabled in both PLS04 builds.

Windows `WorkingSetSize` is resident working set (RSS equivalent), including
shared pages. A separate observer thread requests a 2 ms polling interval;
Windows scheduling/timer resolution can stretch that interval. Boundary samples
are also taken. The sampled peak is a lower bound; polling may miss a short
spike. `PeakWorkingSetSize` is the OS high-water mark for the entire process,
including Header/lexical preparation; both its start and end are recorded.
`PrivateUsage` is private committed memory, not resident memory. No working-set
trim or allocator purge is performed. The sampler is not an extraction worker.

`PLS04_MEMORY_PATH` selects memory/destruction JSON, `PLS03_PROFILE_PATH` keeps
the same coarse alias profile. Both arms pay the same sampler overhead. The
PLS03-versus-PLS04-control pairs quantify the combined observation/code effect.

File destruction is timed around actual model release; control destruction is
timed around the final locals clear. Summed file+tail destruction is reported
separately. Early destruction is included in semantic linking/lifecycle, not
subtracted. The lifetime ends after fingerprint consumption and destruction of
bound/final arrays. G, identities, strings and file input remain until process
cleanup, matching PLS03. Header/lexical setup, oracle, profile analysis and
report I/O are excluded. This is not a production PUBLISH measurement.

Allocation counts track requested payload and original allocation phase, so
frees are charged to their allocation phase. Peak live requested bytes exclude
pre-existing Header/input allocations, allocator metadata and stack. Retained G
or string allocations created during linking can remain at lifecycle end.
