# RESOLVED-G-02 / PATH-INTERN-COST

Bounded path-interning experiment on the unchanged RG01 operation tape.
All policy code is guarded by `CW_PATH_INTERN_02`; root production targets do not define it.

```powershell
cmake -S experiments/resolved-g-02 -B build/resolved-g-02 -G "Visual Studio 18 2026" -A x64
cmake --build build/resolved-g-02 --config Release --parallel 8 --target ResolvedGPath02
ctest --test-dir build/resolved-g-02 -C Release --output-on-failure
python experiments/resolved-g-02/run.py build/resolved-g-02/measurements --pairs 15 --publish-pairs 15
python experiments/resolved-g-02/calibrate.py build/resolved-g-02/calibration --pairs 10
```

`run.py` accepts `--exe`, `--project`, `--types`. Its default is the existing UnitProXL fixture. It first captures once, runs all replay arms in-process with alternating control/candidate order and rotating case order, then independently tests the best implementable candidate in ordinary full PUBLISH. Every successful PUBLISH in this main series is checked against compiled.bin/runtime.bin digests after the process exits.

The experiment is built completely from current source, not imported Graph objects.

## Cases

| Mask | Case | Meaning |
|---:|---|---|
| 0 | Control | Existing Graph algorithm |
| 1 | Prehashed | Diagnostic: query hashes computed before replay; growth/rehash still uses normal hashing |
| 2 | Reserve vectors | Diagnostic: know final path/step vector capacities; reserve inside `g_ms` |
| 4 | Reserve index | Diagnostic: know final unique count; prepare final-sized index inside `g_ms` |
| 6 | Both reservations | Diagnostic combination of 2 and 4 |
| 8 | Single probe | Candidate: preserve the first empty slot from lookup and reuse it for insertion when index size did not change |
| 16 | Cached rehash hashes | Candidate: append an auxiliary 64-bit hash per new canonical path and use it during index growth |
| 24 | Both candidates | Candidate combination of 8 and 16 |
| 32 | Skip existing | Diagnostic: return the known handle for existing-path calls, preserving new-path work |
| 64 | Prebuilt paths | Diagnostic: copy final path records, steps and index before the timer, then skip path API calls |

The environment variable `CW_PATH_INTERN_MODE` permits only 0, 8, 16 and 24 on the ordinary PUBLISH path. Oracle cases are selected only inside replay, after normal Source semantics have completed. They are not production candidates.

## Contracts

- Same chronological resolved operation list; same canonical handles.
- Exact original Graph arrays and all original index fields/counters, checked after each replay.
- Warmups also compare every returned handle/replacement flag.
- Additional cached hashes are recomputed and verified outside the timer.
- Hash function, fingerprint and index growth policy are unchanged.
- Single-probe falls back to the original insertion lookup after rehash changes index size.
- Allocation failure rolls back appended records/steps and the auxiliary hash array.
- Auxiliary hashes are transient and do not change persistence.
- No per-call timer. `g_ms` covers full tape dispatch, ordinary resolved APIs, candidate allocations and reservation for cases 2/4/6. Header cloning, exact parity checking, hash-cache verification, release, external digests, prehash planning and oracle catalogue copying are excluded.

Ablation differences are not exclusive CPU costs and cannot be added/subtracted to derive a precise existing/new breakdown: occupancy, allocator and cache behavior change. Cases 1/2/4/6 require future information or work excluded from the timer. Case 64 deliberately makes the future path catalogue available early; it measures an avoided-work bound, not a valid replacement Source algorithm.

The diagnostic tape remains RG01's 96-byte record; it is not a new production IR. Existing/new classification and query hashes are prepared once outside the replay timer.

## Validation

The small fixture covers repeated initialization targets, repeated link targets, repeated paths, array types, extern completion and string batches. All nine cases must pass exact parity, deliberate corruption must fail, and all three implementable candidates must preserve ordinary PUBLISH artifacts.

The real-corpus driver performs two warmup pairs plus 15 measured pairs per case and two warmup pairs plus 15 measured full-PUBLISH pairs for the selected candidate. Bootstrap intervals are paired-median intervals, not differences of independent medians.

Absolute timing depends on launch/runtime conditions. Use fresh paired controls. Historical RG01's 153.3827 ms is a reference, not a denominator to reuse in a new session. The native replication command below provides a short launch-condition check:

```powershell
$env:CW_RESOLVED_G_CAPTURE='1'
$env:CW_RESOLVED_G_REPEATS='3'
$env:CW_PATH_INTERN_MODE='0'
& build/resolved-g-02/engine/Release/ResolvedGPath02.exe publish-timeline build/publish-subsecond-01/fixture/project.json 24027 > build/resolved-g-02/native-replication.log
Remove-Item Env:CW_RESOLVED_G_CAPTURE, Env:CW_RESOLVED_G_REPEATS, Env:CW_PATH_INTERN_MODE
```
