# PUBLISH performance review

## Measurement

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

## Changes

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

## Remaining work

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
