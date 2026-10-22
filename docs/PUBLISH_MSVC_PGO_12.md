# MSVC PGO: measured PUBLISH improvement

An isolated PUBLISH benchmark build with MSVC profile-guided optimization
improves complete PUBLISH on the full UnitProXL fixture. Clang was not installed
in the checked locations; the user chose to evaluate MSVC only for now.

## Controlled comparison

MSVC 19.51 / Visual Studio 2026, x64 Release `/O2 /Ob2`, IPO enabled for both
variants. Same source, fixture and experimental feature switches. Native
execution outside the filesystem sandbox; no concurrent builds or tests.
Eight alternating ABBA/BAAB blocks with two runs of each variant per block,
plus one excluded warmup block:

| Metric | Result |
|---|---:|
| Ordinary Release mean PUBLISH | 1449.176 ms |
| PGO mean PUBLISH | 1334.593 ms |
| Mean block saving | 114.583 ms (7.91%) |
| Student-t 95% interval for saving | [99.659, 129.506] ms |
| Blocks favoring PGO | 8 / 8 |

This establishes an improvement for this executable and workload, not a promise
of the same percentage for other projects or the production server executable.
Subsecond PUBLISH remains unachieved.

Three additional paired instrumented runs measured the following means. These
are a separate series; nested stages must not be added together.

| Stage | Release ms | PGO ms |
|---|---:|---:|
| Complete PUBLISH | 1458.197 | 1343.470 |
| Source | 487.575 | 436.999 |
| Header | 234.524 | 192.277 |
| Whole semantic pass | 809.244 | 717.794 |
| Persistence | 212.031 | 178.984 |
| Parallel physical-plan wall time | 84.422 | 70.510 |
| Resident construction | 202.230 | 203.007 |

## Training and validation

Training used three complete PUBLISH runs of the full fixture, followed by
the simple (1000 types) and mixed (501 types) fixtures. The selected training
mode was `/FASTGENPROFILE:NOEXACT`; counters are not atomic, so concurrent
counts can be approximate. Thread-safe `/GENPROFILE:EXACT` and
`/FASTGENPROFILE:EXACT` attempts exceeded the initial 30-second limit and were
stopped. Their data was not merged into the selected profile. Full successful
NOEXACT training runs took about 25.8 seconds each; instrumented timings are
not included in the comparison.

Five `standard!*.pgc` files were merged explicitly into `standard.pgd` using
the matching MSVC `pgomgr`. Its report recorded nonzero dynamic counts and no
counter loss due to overflow. The optimized link used `/USEPROFILE:PGD=...`;
the linker confirmed 17688 of 17691 functions optimized using profile data.
The final executable has no pgort140.dll dependency.

Validation included:

- Nine integration scenarios, including an expected failure with identical
  diagnostics and no compiled artifact.
- All 44 full-project ABBA/stage benchmark runs, including warmups, produced
  matching compiled.bin and runtime.bin hashes and passed baseline audits.
- Full REBUILD byte parity between ordinary and PGO builds, independent audits,
  then no-change BUILD without fallback and with unchanged artifacts.
- Final PUBLISH removed BUILD acceleration state and passed a baseline audit.

REBUILD/BUILD checks establish correctness, not a performance improvement.
A single uninstrumented check observed REBUILD at 7828 ms ordinary versus
8688 ms PGO, and no-change BUILD at 368 versus 380 ms. These are not controlled
series. The profile was trained for PUBLISH, so it must not be promoted as a
general server profile without training and measuring LOAD/BUILD/REBUILD too.

The main server build remains ordinary Release. PUBLISH's USN/flush policy and
all semantic/persistence formats are unchanged.

## Reproducing the build

New CMake cache settings apply only to `ServerEngineV4PublishBenchmark`, only
in Release, and require MSVC plus supported IPO:

```text
SERVER_ENGINE_PUBLISH_PGO=OFF       # default, ordinary Release
SERVER_ENGINE_PUBLISH_PGO=GENERATE  # FASTGENPROFILE:NOEXACT
SERVER_ENGINE_PUBLISH_PGO=USE       # USEPROFILE
SERVER_ENGINE_PUBLISH_PGO_DATABASE=<absolute .pgd path>
```

Example in PowerShell, using a fresh build directory and a disposable project
copy. Keep the same sources, compiler and object files between GENERATE and USE.

```powershell
$buildDir = 'C:/Boris/ServerEngineV4/build/publish-pgo-repro'
$database = "$buildDir/publish-benchmark.pgd"
$project = 'C:/Boris/ServerEngineV4/build/publish-subsecond-01/fixture/project.json'

cmake -S . -B $buildDir -G 'Visual Studio 18 2026' `
  -DSERVER_ENGINE_BUILD_BENCHMARKS=ON `
  -DSERVER_ENGINE_PUBLISH_PGO=GENERATE `
  "-DSERVER_ENGINE_PUBLISH_PGO_DATABASE=$database"
cmake --build $buildDir --config Release --target ServerEngineV4PublishBenchmark `
  --parallel 8 -- /p:UseMultiToolTask=true /p:CL_MPCount=8

# Locate the matching compiler's PGO tools/runtime from this build's linker.
$linkerLine = Get-Content "$buildDir/CMakeCache.txt" |
  Where-Object { $_ -like 'CMAKE_LINKER:FILEPATH=*' }
$toolDir = Split-Path ($linkerLine -split '=', 2)[1]
Copy-Item "$toolDir/pgort140.dll" "$buildDir/Release/pgort140.dll"

1..3 | ForEach-Object {
  & "$buildDir/Release/ServerEngineV4PublishBenchmark.exe" publish $project 24027
  if ($LASTEXITCODE -ne 0) { throw 'Training failed' }
}
foreach ($case in @(@('simple', 1000), @('mixed', 501))) {
  $small = "C:/Boris/ServerEngineV4/build/publish-subsecond-01/$($case[0])/project.json"
  & "$buildDir/Release/ServerEngineV4PublishBenchmark.exe" publish $small $case[1]
  if ($LASTEXITCODE -ne 0) { throw 'Training failed' }
}
# Successful exits write .pgc files beside the executable.
$counts = @(Get-ChildItem -Force "$buildDir/Release" -Filter 'publish-benchmark!*.pgc' |
  ForEach-Object FullName)
& "$toolDir/pgomgr.exe" /merge @counts $database
if ($LASTEXITCODE -ne 0) { throw 'Profile merge failed' }

cmake -S . -B $buildDir -DSERVER_ENGINE_PUBLISH_PGO=USE
cmake --build $buildDir --config Release --target ServerEngineV4PublishBenchmark `
  --parallel 8 -- /p:UseMultiToolTask=true /p:CL_MPCount=8
```

Regenerate and retrain when sources or the compiler change. Do not reuse this
benchmark database for another executable. The CMake options deliberately do
not propagate to the server or test targets. The selected USE configuration
produced the exact same executable hash as the measured global-link-option
prototype; GENERATE target isolation and the default OFF build were checked.

Raw profiles, logs, scripts, measurements and the preserved executable are in
`build/publish-msvc-pgo-12/` (`pgo.exe`). Ordinary executable SHA-256:
`FAB41C70133B94F778394CACA0BC08A358CCCDCD8D7154F4E4166E21577D0595`.
PGO executable SHA-256:
`2C805C38EA443C6EE304BFB4775BB58BC405C5D1E29850C716FAD6876F90554E`.

References: [MSVC PGO workflow](https://learn.microsoft.com/en-us/cpp/build/profile-guided-optimizations?view=msvc-170),
[instrumentation options](https://learn.microsoft.com/en-us/cpp/build/reference/genprofile-fastgenprofile-generate-profiling-instrumented-build?view=msvc-170),
[/USEPROFILE](https://learn.microsoft.com/en-us/cpp/build/reference/useprofile?view=msvc-170).
