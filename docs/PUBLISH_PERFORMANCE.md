# PUBLISH performance review

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
