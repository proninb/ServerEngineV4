# SEMANTIC-BIND-02 — Scaling & Memory Audit

Diagnostic experiment, not a scheduler, Parser rewrite or semantic extension. Production PUBLISH is unchanged.

```powershell
cmake -S experiments/semantic-bind-02 -B build/semantic-bind-02 -G "Visual Studio 18 2026" -A x64
cmake --build build/semantic-bind-02 --config Release --target SemanticBind02Timing SemanticBind02Allocations --parallel 8
ctest --test-dir build/semantic-bind-02 -C Release --output-on-failure
python experiments/semantic-bind-02/run_audit.py build/semantic-bind-02/Release/SemanticBind02Timing.exe build/semantic-bind-02/Release/SemanticBind02Allocations.exe build/publish-subsecond-01/fixture/project.json build/semantic-bind-02/new-results 3
python experiments/semantic-bind-02/analyze.py build/semantic-bind-02/new-results
```

Use native execution outside the sandbox. Do not build or test concurrently with measurements. Each output directory must be new. The matrix executes fresh processes sequentially, rotates/reverses worker configurations, alternates paired modes, excludes one warmup per timing mode/configuration and runs allocation diagnostics separately. Three pairs per 1/2/4/8/16 configuration are the default: a diagnostic sample, not a precision speedup study. The analyzer has explicit UnitProXL inventory assertions.

## Executables and observer controls

`SemanticBind02Timing` has no replacement allocator and no request lifecycle atomics. In `control` mode it calls the unchanged SEMANTIC-BIND-01 `run()`; in `timing` mode it additionally records per-root task begin/end and Windows `QueryThreadCycleTime` deltas. Inputs, scheduling policy, Parser, canonicalizer, Binder and numbering are the same. Both modes independently verify WHO/field-type projection with Header V2, including negative outcomes if supplied.

The valid observer comparison for Parser scaling is **`parse_ms`**, measured before memory snapshots. Profiled `model_total_ms` also includes repeated memory walks and snapshots between stages: do not treat it as an optimized pipeline or as equivalent to the uninstrumented total. Post-parse snapshots can change caches for later stages too.

`SemanticBind02Allocations` replaces all standard new/delete forms (ordinary, array, aligned, sized and nothrow). Allocation headers retain stage/root ownership across phase/thread boundaries. Exact requested-byte/call/live/peak counters use atomics. PMR buffers in CANONICALIZER-01 use a diagnostic-only upstream hook because MSVC `new_delete_resource` can bypass replaceable global new. This is not a general CRT malloc, VirtualAlloc or OS working-set profiler.

The allocation executable additionally enables an empty request-lifecycle marker. Compile-time size/alignment checks ensure that its `request` ABI is the same as the ordinary layout. Constructor/copy/move/destructor accounting measures simultaneously live instances, including moved-from worker records and transient reallocation copies. These atomics, allocation headers, malloc clock probes and changed PMR upstream **perturb execution**; allocation-run durations, root timings and cycles are not native performance evidence. `malloc_ns` includes clock-probe overhead and excludes later bookkeeping/free cost; it cannot by itself establish allocator's percentage of normal Parser time.

Two guarded hooks were added to experiment headers only: `SB02_REQUEST_LIFETIME` in SEMANTIC-BIND-01 and `SB02_ALLOCATIONS` in CANONICALIZER-01. Both are absent from ordinary targets. No production Parser or scheduler change is present.

## Artifacts per observation

- `summary.csv`: coarse stage times, input inventory, peak process working set sampled before oracle, lifecycle peak and parity verdict.
- `roots.csv`: root/file/path, effective tokens, task owner, relative task interval, wall time, CPU-cycle delta, output records/capacity; allocation ownership in the allocation executable.
- `workers.csv`: Parser task counts/tokens, sum of task wall intervals, first/last interval, initial dispatch delay, between-task gaps and tail/non-task wall intervals. **These are wall-time accounting intervals, not an OS blocked/runnable-state measurement.** Task wall time includes preemption or waits inside a task. Empty root tasks remain in the inventory; idle workers have zero tasks.
- `types.csv`, `request-layout.csv`: actual native sizes, member offsets and padding evidence.
- `arrays.csv`: logical size and capacity bytes for primary local/batch/result arrays, modifier buffers, forest adjacency, representative/WHO outputs at phase boundaries. Subobject/vector headers inside records are already included in record size and must not be added again. These arrays are not a complete allocation census: allocator counters cover additional temporary maps and lookup structures.
- `lifetimes.csv`: cumulative peak/current tracked requested bytes and exact instrumented request-instance counts after parse, before/after local release, canonicalization, binding and numbering. Moved modifier payloads are not counted twice; moved-from request objects still exist.
- `allocations.csv`: calls and cumulative requested bytes by allocation origin; maximum concurrently live requested bytes owned by each origin, live allocation count and gross malloc probe time. Origin peaks happen at different instants and cannot be summed to obtain global peak. The total row includes preparation and pool startup. Live bytes must return to zero after explicit teardown of tracked input/result/pool.

Requested bytes exclude diagnostic allocation headers, native allocator metadata/slack and mapped file bytes. Reported process working set includes such costs and all process input/preparation state; it is not model-only memory. The retained SEMANTIC-BIND-01 145.7 MiB subset is reconstructed independently from array capacities, not confused with process peak or total temporary live storage.

The driver preserves executable/source hashes, source snapshots and measured binaries. Exploratory directories (including the first allocation probe without the PMR hook) must not be mixed with the final matrix.

## Validation

Both executables run the existing 270 model checks. Allocation calibration checks aligned allocation, cross-thread deallocation and exact request copy/move destruction counts. `test_audit.py` executes six real Header V2-verified audit/control fixtures and checks worker accounting, arrays, lifecycle boundaries and zero remaining tracked allocations. Every real-corpus observation also runs the independent Header V2 oracle. Coverage remains the same partial Header type-binding projection as SEMANTIC-BIND-01.
