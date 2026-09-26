# PUBLISH performance review

## Mixed-workload runtime plan growth (2026-09-26)

The current mixed fixture has 10,001 types, 170,001 members, 20,000 objects,
and 10,000 links. Generate it with `new_publish_fixture.ps1`; older generated
fixtures may link ordinary integer members and are rejected by the current
reference-target rules. The historical mixed results below use a different
fixture and are not a directly comparable baseline.

Stage timing isolated the multi-second LOAD/PUBLISH delay to runtime
materialization: approximately 2.6 ms for layout versus 3,905 ms for
materialization. `prepare_record_plan` reserved exactly the accumulated member
count plus the next record's members before appending each record. With many
distinct types, this repeatedly reallocated and copied all previous plans,
producing quadratic copying for fixed-size records.

Removing that exact-size reservation lets `std::vector::push_back` use geometric
growth. The existing count bounds, allocation-failure handling, and rollback
remain in place. Diagnostic materialization time fell to approximately 10.8 ms.
No persisted format or validation policy changes are involved.

With the temporary timers removed, an alternating A/B comparison (one warmup
and seven measured runs per executable, Release IPO) measured PUBLISH medians
of 3,378.59 ms before and 95.8247 ms after: 35.3x faster, a 97.2% time reduction.
The candidate won all seven pairs. Both executables include the same existing
local runtime work; only the reservation above differs. The baseline executable
audited every image outside the timer, and every SHA-256 matched
`0D9DE075B2EF1D5767F7C1405BED60B4C2D6D219B1F079AD68D8CDEEC6AF123C`.
Raw results are in `build/publish-mixed-growth-ab.csv`. A subsequent candidate
LOAD took 12.77 ms (single run). All eight Release CTest suites and the six
runtime scale cases (three runs each) pass.

Performance coverage must include many distinct record types, not only many
objects of one type: the existing `objects`, `links`, and `chain` runtime
scenarios each contain one record type and do not expose this growth pattern.

## Pass 3: semantic streaming hot path

This pass starts from `20885736`, with Release IPO enabled. It changes only
construction-local frontend/Source Map work; persisted format and semantic output
remain unchanged.

Three hot-path changes are combined:

- Source inputs, directive-free Header inputs, and preprocessed Header inputs use
  separate token-stream paths. A Header with no configured predefines and no
  lexical directives bypasses preprocessor/executor checks entirely.
- Identifier spelling caches the current physical file source view instead of
  resolving `file_context` storage on every identifier. The cache is invalidated
  before include materialization can grow source storage.
- Source Map physical-file contribution counts are accumulated while
  contributions are accepted. PUBLISH therefore does not rescan every
  contribution during finalization merely to rebuild those counts. Contribution
  lookup also reuses one computed owner hash across lookup and insertion.

The baseline executable is the saved `20885736` build. Each workload uses one
warmup followed by seven measured runs per executable, alternating order in
separate processes. The baseline executable performs the cold audit after every
candidate and baseline PUBLISH outside the lifecycle timer; SHA-256 must also
match for every image.

| Workload | Baseline median | Candidate median | Median reduction | Paired wins |
|---|---:|---:|---:|---:|
| 100,000 single-member types | 72.1047 ms | 69.1196 ms | 4.1% | 6 / 7 |
| 1,000,000 single-member types | 721.632 ms | 657.874 ms | 8.8% | 7 / 7 |
| Mixed: 10,001 types, 160,001 members, 20,000 objects, 10,000 links | 70.5461 ms | 70.0154 ms | 0.8% | 5 / 7 |

The million-type result is the meaningful gain: the candidate wins every paired
run and reduces the median by 63.758 ms. The 100K result is smaller but mostly
consistent. The mixed result is effectively neutral within run variability;
there is no measured mixed-workload regression from the specialized
preprocessed path.

Median peak working set is essentially unchanged on the simple workloads:
39,751,680 -> 39,874,560 bytes at 100K and
321,224,704 -> 321,417,216 bytes at 1M. The mixed median changes from
33,259,520 to 33,652,736 bytes.

All measured images within each workload are byte-identical:

- 100K: `2A6810050FE40130A506B865AAA02F3EDC6E24A93C7F0C051E566D815144FB43`
- 1M: `5DCED4F63D7DC34B783F78C63E3355BC772692DCB66EC930473E3C62384A90E9`
- mixed: `EA6DB1B028BEDF69358380C35249035B3B7BB95A0974919A88C45B1725DB09E4`

Release build, CTest, and `git diff --check` passed before measurement.

`compare_publish.ps1` now resolves a non-existent relative `ResultPath` through
the PowerShell provider path, so benchmark output is anchored to the caller's
PowerShell location rather than the process working directory.

The remaining dominant work on the million-type fixture is serial semantic
construction plus compiled-image encoding. Further changes should be driven by
new stage measurements rather than speculative capacity reservation.

## Pass 2: Release interprocedural optimization

CMake now checks compiler/linker IPO support and enables it for Release and
RelWithDebInfo when available. `-DSERVER_ENGINE_ENABLE_IPO=OFF` restores ordinary
optimization. Debug flags are not changed. This permits cross-translation-unit
optimization of the frontend/parser/table calls without changing their APIs or
the persistence contract. The tradeoff is additional optimized build/link work.
An unsupported toolchain emits a warning and uses ordinary optimization.

The baseline is the saved Pass 1 final executable (production code at
`981c8c7`); the candidate uses the same production code with MSVC IPO enabled.
Windows x64 Release, seven measured runs after one warmup per executable,
alternating order, separate processes, warm OS cache. Full audit by the baseline
executable and SHA-256 checks occur outside the lifecycle timer. Both binaries
receive the exact same project paths and input files.

| Workload | Baseline median | IPO median | Time reduction |
|---|---:|---:|---:|
| 100,000 single-member types | 80.414 ms | 79.835 ms | 0.7% |
| 1,000,000 single-member types | 809.951 ms | 752.957 ms | 7.0% |
| Mixed: 10,001 types, 160,001 members, 20,000 objects, 10,000 links | 77.470 ms | 75.983 ms | 1.9% |

The million-type candidate wins all seven paired comparisons. Improvements on
the smaller workloads are within ordinary run variability; they are not evidence
of a large general speedup. Absolute timings from different passes should not be
combined into a cumulative percentage. Peak working set remains about 38 MiB /
307 MiB for simple inputs and 32 MiB for the mixed input.

Raw milliseconds, in run order:

| Workload / variant | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---:|---:|---:|---:|---:|---:|---:|
| 100K baseline | 82.3099 | 78.5200 | 78.1678 | 88.1741 | 80.4135 | 83.2641 | 80.1506 |
| 100K IPO | 79.8348 | 76.7304 | 78.6421 | 82.3623 | 79.9172 | 73.2850 | 80.9289 |
| 1M baseline | 849.246 | 829.064 | 772.245 | 815.997 | 809.951 | 783.145 | 770.142 |
| 1M IPO | 743.422 | 777.208 | 750.544 | 763.380 | 771.357 | 730.775 | 752.957 |
| Mixed baseline | 77.7599 | 77.0981 | 74.9855 | 77.4702 | 81.6678 | 75.8746 | 77.5868 |
| Mixed IPO | 76.7821 | 74.1877 | 78.7588 | 75.7915 | 81.3110 | 75.9825 | 74.4193 |

All 42 measured images pass the cold audit and match within each dataset. Simple
input hashes are unchanged from Pass 1. The mixed image hash at
`benchmark/mixed_10000/project.json` is
`EA6DB1B028BEDF69358380C35249035B3B7BB95A0974919A88C45B1725DB09E4`.
Persisted paths are part of the image, so relocating a fixture can change its hash.

The complete Release build and all six CTest suites pass with IPO enabled.
Mixed-fixture REBUILD and PUBLISH both pass a baseline-executable audit and
produce the same bytes; PUBLISH removes the REBUILD acceleration files. A
separate CMake configuration confirms that the OFF switch omits IPO flags.
The fixture/comparison scripts also pass a small simple-fixture smoke run with
uneven file partitions and an even measurement count.

### Reproduce

Run from the repository root in PowerShell. The generator refuses an existing
output directory; comparison refuses an existing results file. Use dedicated
fixtures because PUBLISH replaces their persisted artifacts.

```powershell
cmake -S . -B build-no-ipo -DSERVER_ENGINE_BUILD_BENCHMARKS=ON -DSERVER_ENGINE_ENABLE_IPO=OFF
cmake --build build-no-ipo --config Release --target ServerEngineV4PublishBenchmark
cmake -S . -B build -DSERVER_ENGINE_BUILD_BENCHMARKS=ON -DSERVER_ENGINE_ENABLE_IPO=ON
cmake --build build --config Release --target ServerEngineV4PublishBenchmark
./benchmarks/new_publish_fixture.ps1 -OutputDirectory build/fixtures/mixed -Scenario mixed -TypeCount 10000 -FileCount 64
./benchmarks/compare_publish.ps1 -BaselineExe build-no-ipo/Release/ServerEngineV4PublishBenchmark.exe -CandidateExe build/Release/ServerEngineV4PublishBenchmark.exe -Project build/fixtures/mixed/project.json -ExpectedTypes 10001 -ResultPath build/mixed-comparison.csv
```

`simple` generates one-member structs (expected types = TypeCount). `mixed`
generates sixteen-member records with defaults and constructors, two guarded
includes of a shared type in every header, and two objects plus one link per
record. It places Source roots before Header roots in project order to exercise
semantic domain ordering. Its expected type count is TypeCount + 1. These remain
synthetic fixtures, not a representative production corpus.

### Rejected reservation experiment

Two lexical-token-based hints reserved the string and identity vectors, byte
arena and hash indexes before parsing. Their speculative storage caps were
approximately 18 MiB and 70 MiB. Neither showed a stable million-type benefit:
paired-series medians were 818.695 -> 812.755 ms for the smaller cap, and
787.179 -> 791.246 ms for the larger cap (seven runs each). The smaller cap also
regressed 100K inputs. Both experiments were removed, including their APIs.
Graph reservation was not implemented because raw token count does not reliably
predict the distribution of types, members, objects and links.

Temporary instrumentation of the ordinary optimized baseline measured 440–466
ms for semantic parsing/finalization, 147–150 ms for lexical/assignment
preparation, 145–159 ms for encoding, and 43–44 ms for flush on the million-type
fixture. Instrumentation was removed before A/B measurement. Semantic decoding
and compiled encoding remain the next targets for profiling; the data does not
justify introducing speculative allocations or parallel shared semantic writes.

## Pass 1: publication, provenance index, and string hashing

The initial allocation work below was committed as `2ebfc4e`. The next series
uses that implementation as its baseline (`scratch`) and independently saved
Release executables for each additional change. Five measured runs per variant
and dataset follow one warmup; ordering alternates forward/reverse. Every image
receives an explicit full audit and SHA-256 check **outside** PUBLISH timing.

| Cumulative variant | 100,000 types median | 1,000,000 types median |
|---|---:|---:|
| Scratch reuse and flat cold-audit deduplication (`2ebfc4e`) | 106.177 ms | 1012.300 ms |
| Remove cold audit and duplicate structural bind from fresh compiled persistence | 81.151 ms | 826.331 ms |
| Add reusable open-addressed Source Map contribution index | 78.697 ms | 764.713 ms |
| Reuse the string hash between lookup and insertion | 74.275 ms | 753.022 ms |

Final time decreases by 30.0% / 25.6% against this series' baseline. For the
million-type workload, the median differences are 186 ms for publication policy,
62 ms for the contribution index, and 12 ms for single hashing. These are
differences of medians, not additive per-stage profiler measurements. The
single-hashing improvement is small relative to run variability (final range
730–817 ms), although the 100K dataset also improves. Peak working set remains
approximately 38 / 307 MiB. All 40 images pass the explicit audit and match the
SHA-256 values recorded below.

Release validation passes all six CTest suites. On both datasets, REBUILD,
explicit audit, PUBLISH, and another audit also produce byte-identical compiled
images; PUBLISH removes the acceleration artifacts produced by REBUILD.

The full cold audit remains available through:

```text
ServerEngineV4PublishBenchmark audit <project.json> <expected-types>
```

This mode measures LOAD plus `verify_contents()`. Normal PUBLISH/REBUILD now
compute section CRCs during encoding, structurally bind, flush, reopen read-only,
and structurally bind for resident publication. They no longer repeat the full
section CRC/semantic audit. This is an explicit change of verification policy,
not merely a faster implementation of the previous publication operation.

The Source Map scratch table has a 50% maximum load factor, preserves the dense
contribution order, and uses generation tags to reset roots in O(1). Full table
clearing occurs only on generation wrap. It still merges declaration/definition
contributions for one physical file within a root and keeps different roots
independent. Tests exercise growth, reverse-order reinsertion, definition
promotion, large-to-small roots, and reset after finalization. String tests cover
growth, duplicate interning, empty/missing names, baseline reuse, and arena-alias
insertion.

A slicing-by-16 CRC experiment did not provide a stable million-type improvement
and was discarded. Production retains slicing-by-8; an independent bitwise CRC
test covers compile-time evaluation, four byte patterns, 16 alignments, short
tails, and lengths through 64 KiB.

Capacity estimation, CRC fusion, and parallel semantic parsing are deferred.
The benchmark datasets remain synthetic single-member records, so realistic
include/object/link workloads must be measured separately.

## Initial allocation optimization (before publication-policy change)

### Measurement

Windows x64, CMake Release, existing `ServerEngineV4PublishBenchmark` target.
Both variants used the same benchmark driver, compiler settings, source paths,
and datasets. The baseline used commit `6ae8ccde6561738ec1e49312f2ff8b459b9cf3fe` versions of `parser.cpp` and
`compiled_project.cpp`; the optimized version changed only those two production
files. Existing local benchmark/CMake changes were retained in both variants.

Each dataset/variant received one warmup, followed by five measured PUBLISH
operations in separate processes. Variant order alternated between pairs.
The OS file cache was not cleared. The timer covers the lifecycle function,
including validation and flush, but excludes process startup and subsequent
benchmark checks. SHA-256 was computed after timing.

| Types | Baseline median | Optimized median | Time reduction | Speedup |
|---|---:|---:|---:|---:|
| 100,000 | 140.130 ms | 103.240 ms | 26.3% | 1.36x |
| 1,000,000 | 1,584.170 ms | 1,146.900 ms | 27.6% | 1.38x |

Raw milliseconds, in run order:

| Types / variant | Run 1 | Run 2 | Run 3 | Run 4 | Run 5 |
|---|---:|---:|---:|---:|---:|
| 100,000 baseline | 140.130 | 142.562 | 139.173 | 151.789 | 138.779 |
| 100,000 optimized | 101.680 | 103.240 | 125.374 | 109.676 | 100.129 |
| 1,000,000 baseline | 1584.170 | 1603.970 | 1623.670 | 1545.800 | 1512.300 |
| 1,000,000 optimized | 1146.900 | 1177.150 | 1150.040 | 1132.390 | 1113.850 |

Peak process working set was essentially unchanged: approximately 38 MiB and
308 MiB respectively. This is a throughput improvement, not a major reduction
in resident memory.

All 20 outputs matched their dataset's baseline SHA-256:

- 100,000: `2A6810050FE40130A506B865AAA02F3EDC6E24A93C7F0C051E566D815144FB43`
- 1,000,000: `5DCED4F63D7DC34B783F78C63E3355BC772692DCB66EC930473E3C62384A90E9`

### Changes

The non-recursive record parser now reuses member, initializer, constructor,
and assignment scratch vectors across definitions and roots. Empty constructor
operation lists no longer allocate an assignment bitmap. On this dataset the
old path performed millions of small temporary allocations.

Persisted source validation now gathers semantic keys in a reusable vector,
sorts them, and checks adjacent duplicates. It previously allocated a hash node
per contribution. Duplicate detection, CRC checks, semantic validation, output
format, and persistence flush remain enabled. Sorting costs O(k log k) for a
root with k contributions; broader workloads must be measured before claiming
the same speedup universally.

Regression coverage checks initialization/member isolation across differently
sized records, empty records, forward declarations and constructors. A
persisted-image test injects a duplicate semantic contribution and recomputes
its CRC to ensure semantic validation still rejects it.

### Remaining work at that stage

Temporary stage timing on the million-type dataset, before the final A/B run,
identified these approximate costs:

| Stage | Before | After accepted changes |
|---|---:|---:|
| Parser, source map, topology | 762–781 ms | 470–480 ms |
| Compiled image validation | 266–274 ms | 206–212 ms |
| Lexical preparation and assignment acquisition | 127–132 ms | 129–140 ms |
| Image creation and encoding | 134–152 ms | 145–161 ms |
| Flush | 37–53 ms | about 44 ms |

These diagnostic runs are separate from the final A/B series; their absolute
times should not be combined with the table above. Instrumentation was removed.

Next candidates are repeated string/index lookups in deep validation, CRC
throughput during encoding and verification, and serial semantic construction.
Parallel semantic parsing requires a design for deterministic shared strings,
identities and graph handles; merely dispatching roots to threads is unsafe.
Removing validation or durable flush would change the contract and is not an
equivalent optimization.

The current fixtures contain independent single-member structs and no objects
or links. Include-heavy inputs, large records, constructors, objects, links and
cold-cache I/O need separate measurements. Current LOAD timing measures mmap
binding, not full graph traversal or full-image validation.
