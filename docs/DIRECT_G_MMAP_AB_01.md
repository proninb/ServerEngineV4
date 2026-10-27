# DIRECT-G-MMAP-AB-01 — no default adoption

Full UnitProXL: **24,027 types, 137,584 objects, 367,144 links**. Windows x64, MSVC 19.51, Release `/O2` + IPO `/GL` + `/LTCG`, no PGO. Date: 2026-10-26.

**Result:** current append/reallocate v27 is slower in total PUBLISH and roughly doubles compiled.bin. It lowers private commit, but not peak working set. The implementation has been rolled back from active engine code, including the feature flag, allocator and counters. The prior RAM G + packed encoder is restored; frozen v27 sources/binaries remain in the local evidence archive. No WHO cache, extra semantic model or C10 change was introduced.

## Native PUBLISH (no phase timers)

15 alternating pairs per corpus after two warmups per arm. A separate 15-pair native UnitProXL confirmation was run because the first series showed large absolute-time drift. All initial samples are retained; no outlier exclusion. Positive paired delta below means v27 is slower. Bootstrap uses 20,000 pair resamples, seed 2701; these intervals do not model every source of host drift.

| Corpus / series | RAM + packed median, ms | Direct v27 median, ms | Paired v27 minus RAM, ms | 95% bootstrap interval, ms | v27 wins |
|---|---:|---:|---:|---:|---:|
| Synthetic — initial | 86.12 | 101.54 | +14.06 | +11.77 to +17.80 | 0/15 |
| UnitProXL — initial | 2833.87 | 2111.39 | +93.39 | +45.89 to +223.07 | 2/15 |
| UnitProXL — confirmation | 1910.85 | 1970.81 | +57.48 | +42.53 to +79.52 | 3/15 |

Paired median relative regression: synthetic 16.62%; UnitProXL initial 4.47%, confirmation 3.01%.

The first UnitProXL run is bimodal/drifting: the ratio of its two marginal medians misleadingly suggests a speedup even though v27 loses 13/15 pairs. Use paired differences and the retained confirmation, not that ratio. Confirmation also has host variation, so absolute milliseconds are not a universal latency promise.

## UnitProXL phase series (separate 15 pairs)

`publish-timeline` enables coarse stage clocks and mmap counters. Native timings above have no allocation observer enabled. Individual medians and the median of a sum need not add up.

| Metric | RAM + packed median | Direct v27 median |
|---|---:|---:|
| Full PUBLISH, ms | 1944.130 | 2012.070 |
| Semantic / Parser, ms | 880.744 | 931.021 |
| Persistence, ms | 205.459 | 213.016 |
| Semantic + Persistence, ms | 1091.736 | 1149.976 |
| compiled_encode interval, ms | 67.699 | 78.405 |

`compiled_encode` means metadata/index finalization on v27, and full encoding on the packed baseline. Removing Graph serialization did not remove this stage or establish a persistence win. Its different implementations and memory access paths also matter; these data do not attribute all regression specifically to vector growth. Explicit durable flush is zero in both arms.

## Memory, growth and file size

| Metric | RAM + packed | Direct v27 |
|---|---:|---:|
| Peak working set, MB (native confirmation) | 2572.85 | 2573.61 |
| Peak private commit, MB (native confirmation) | 625.79 | 550.01 |
| compiled.bin EOF, MB | 151.295432 | 316.735488 |
| Actual NTFS allocation, MB | 151.298048 | 316.735488 |
| Column reallocations | N/A | 380 |
| Column allocations including first allocation | N/A | 394 |
| Moved live data, MB | N/A | 149.350292 |
| Unused compiled.bin space, MB | N/A | 165.440581 |
| File extent growth events | N/A | 166 |

MB = 1,000,000 bytes. Private commit is Windows `PeakPagefileUsage`; it is not total RAM. Growth counters for the old RAM G are not instrumented and are N/A, rather than a claim of zero heap copies.

Counter definitions:

- `mmap_growth_count`: retired mapped Graph allocations during column growth, excluding first allocations, metadata and teardown.
- `mmap_moved_bytes`: live element bytes observed before new allocation and charged when the old view is retired; partially filled reserve counts live size, not capacity. Covered by a reserve(4), size=2, reserve(16) regression expecting exactly 8 moved bytes.
- `mmap_file_growth_count`: successful allocator requests extending file capacity, including initial extent and metadata. It differs from vector reallocation count.
- `mmap_unused_bytes`: final EOF minus the 1,344-byte format prefix and sum of all 33 live directory section payload lengths. Includes retired allocations, unused capacity, alignment and final extent slack.

Native v27 keeps coarse file extent counters but has no per-allocation observer. The profile observer walks 14 column descriptors only on allocation, never per record. The actual NTFS allocation was queried outside the timing series using `FILE_STANDARD_INFO.AllocationSize`. The raw initial `compiled_allocated_bytes` field came from GetCompressedFileSizeW and equals EOF for these normal files; use physical-allocation.json for physical allocation.

## Equivalence and controls

- One immutable common source corpus and one identical project.json per corpus for both arms. Separate artifact roots are injected by the same experiment-only layout shim. No input path normalization is needed in comparisons.
- Baseline restores the ten original storage/persistence files from Git `2ed70d627e3650a00f8d697745b053986a8f40f2`. Other existing checkout changes, settings and benchmark output hooks are common. This is the old std::vector RAM G + packed encoder, not REBUILD or a timing estimate.
- Every timed run compares hashes/counts/record sizes of all 33 live compiled sections and the complete runtime.bin, outside the operation timer. Payload equality includes G, WHO/WHERE, construction values, indexes, SourceMap, strings and identities. Only physical section offsets, prefix/version and slack are excluded.
- PUBLISH, cold audit and repeat LOAD produce the same raw SHM SHA-256 in both arms. Audit runs verify_contents(). Initial/final audits are outside latency samples.
- No builds/tests run during timing. Both frozen executable SHA-256 hashes, complete source manifests and all corpus files are retained and rechecked. All samples are fresh processes; OS caches are warm, not evicted.
- Tests compiled_project/source_map pass, including the growth-counter regression. No speedup or default adoption is justified by these results.

## Reproduction and retained data

Commands and input preparation: `experiments/direct-g-mmap-ab-01/README.md`.

Local retained directory: `build/direct-g-mmap-ab-01/`:

- `summary.json`, `confirmation-summary.json`, `samples.json`, `confirmation-samples.json`, `samples.csv` — medians, paired intervals and every sample.
- `logs/`, `run.log`, `confirmation.log` — stdout/stderr for each operation.
- `snapshot.json`, `*-source-manifest.json`, `input-manifest.json`, `host.json` — source/build/input/host records.
- `binaries/`, `baseline-source/`, `direct-source/`, `*-build.log` — frozen executables, exact sources and build evidence.
- `parity.json`, `checks.json`, `physical-allocation.json` — semantic payload/runtime/SHM hashes, audits and actual file allocation.

The stored snapshots predate the temporary default-OFF gate and subsequent full rollback. The frozen direct executable always takes the measured v27 branch. The architecture implementation was not optimized during the timing series. Active engine code and tests were restored after the experiment; existing unrelated checkout changes were preserved.
