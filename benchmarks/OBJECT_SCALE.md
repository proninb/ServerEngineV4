# Object scale lifecycle benchmark

Run from the repository root after building the Release benchmark target:

```powershell
cmake --build build --config Release --target ServerEngineV4PublishBenchmark
.\benchmarks\run_object_scale.ps1
```

Defaults: 1,000 / 10,000 / 100,000 types, 10 objects per type.
Each type has one `int value` member. There are no links, inheritance,
virtual methods, or explicit initializers. Each fixture has 128 header roots
and 128 source roots (fewer for small smoke fixtures). The streaming C#
generator avoids retaining the generated text in memory.

The runner copies the executable into a unique results directory, records its
SHA-256 and Git HEAD, and runs sequentially:

1. PUBLISH from fresh input.
2. REBUILD to create persisted BUILD lineage.
3. BUILD with no changes.
4. Compiled artifact audit, outside lifecycle timing.
5. BUILD after renaming the first object in the first source root. Object and
   type counts stay constant; the invalidation unit is the entire source root.
6. Audit after successful changed BUILD.

Each operation runs once in a fresh process. These are exploratory measurements,
not medians or cold-disk measurements. Generation and post-operation count checks
are outside `total_ms`. File-system caching is not reset between operations.
The executable checks lifecycle artifact contracts and type counts; the runner
also checks object counts. CSV results and per-operation stdout/stderr are saved
as each operation finishes. Failure statuses are retained, not converted into
successful timings. The final script exit code does not summarize CSV failures.

Defaults are a 10 GiB per-process memory guard and a 30-minute operation timeout.
The runner samples private memory and working set every 250 ms, so the guard can
overshoot. It is an operational limit, not a claim about maximum engine capacity.
BUILD is skipped if REBUILD did not produce usable lineage. Increase limits only
on a machine with sufficient available resources, for example:

```powershell
.\benchmarks\run_object_scale.ps1 -TypeCounts 1000000 -ObjectsPerType 100 -MemoryLimitGiB 64 -TimeoutSeconds 7200
```

Use a new output directory for every run. Do not run lifecycle benchmark
processes concurrently: the executable uses a fixed shared-memory name/address.
For a small complete workflow check:

```powershell
.\benchmarks\run_object_scale.ps1 -TypeCounts 2 -ObjectsPerType 1
```
