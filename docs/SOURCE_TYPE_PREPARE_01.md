# SOURCE-TYPE-PREPARE-01

2026-10-23. Isolated resolver audit and type-preparation cache experiment completed. **No production adoption:** same-executable measurements show a small positive signal, but the separate production-candidate comparison is inconclusive. Existing Source V2 remains the baseline.

## Audit before changing the algorithm

Two full UnitProXL PUBLISH calls on a separate counter build, before implementing the candidate. No per-call clocks. Counts include exact unique input types and member-transition keys; exact-key diagnostic sets add allocation/CPU overhead, so diagnostic wall times are not optimization evidence.

| Counter | Both baseline calls |
|---|---:|
| Record preparation calls | 1,499,151 |
| Unique raw input type_ref values | 14,201 |
| Ordinary named inputs | 1,466,335 (97.81%) |
| Derived/reference inputs | 32,816 |
| Successful preparations | 1,499,151 |
| Member-transition requests | 1,499,151 |
| Existing member-cache hits | 1,389,153 (92.66265%) |
| Empty-slot/unallocated misses | 52,927 |
| Occupied different-key misses | 57,071 |
| Unique member-transition keys | 108,136 |
| Recursive inheritance lookup calls | 115,180 |
| Base steps copied on member-cache hits | 30,901 |
| Member steps emitted | 1,499,151 |
| Dereference steps emitted | 32,816 |
| Array-index steps emitted | 52,664 |

There are 109,998 member-cache misses, only 1,862 above the number of unique keys. On this successful single-resolver pass every new key first misses, so recurring misses are a small part of the workload. An occupied-slot miss is not necessarily harmful repeated eviction: a first-seen key can also replace an occupied slot. These counts do not justify redesigning the member-cache hash or enlarging the table.

Raw baseline audit: `build/source-type-prepare-01/audit-baseline-20261023/`. Both calls preserve artifact goldens; audit set allocation failures are zero.

## Candidate

Resolver-local positive-only mapping `type_ref -> type_handle + dereference`. Full raw type_ref participates in key equality, including derived/named kind and qualifiers. A lazy direct-mapped vector contains 4096 twelve-byte entries (49,152 bytes). One multiplicative 32-bit slot mix is used; slot count and algorithm were fixed before timing, not tuned to measured speed.

A hit supplies the same preparation outputs at the same grammar point. `prepare_member()` still appends a caller-specific dereference step and rejects invalid member access before consuming the next identifier. Member resolution, ancestry, Graph mutation, dependencies, bounds checks, WHO and physical origins are unchanged.

Only successful catalog resolutions are stored. Negative results are not cached, preserving behavior if a previously absent identity is appended later. Cache allocation failure is swallowed and permanently falls back to original preparation for that resolver. Header record identity-to-handle mappings used by Source remain valid during the pass. Resolver instances do not share mutable state.

Standalone A/B targets define `CW_SOURCE_TYPE_PREPARE_AB` and select `CW_SOURCE_TYPE_PREPARE=0/1`. The separate native candidate defines only `CW_SOURCE_TYPE_PREPARE_V2`; it has no runtime selector or diagnostic sets/counters. Neither macro is enabled by root CMake or the production VS project. Source string scratch reuse and Runtime experiments remain disabled.

## Candidate audit

Two calls per mode in `build/source-type-prepare-01/audit-candidate-20261023/`. Each candidate produces:

- Preparation hits: **1,484,530 / 1,499,151 = 99.02471%**.
- Misses: **14,621**, only 420 above unique input count.
- All original member/cache/lookup/path-step counts match control exactly.
- Both compiled/runtime goldens and type/member counts match; audit allocation failures are zero.

High hit rate proves reuse, not the original operation's cost. In production `graph::named(type_ref, ...)` checks the kind and uses `find_type()` through the existing identity-location array. Ordinary named inputs do not walk qualifiers or reference chains. Replacing this already direct lookup by another cache has limited demonstrated potential.

## Counter-free same-executable series

MSVC Release /O2 /Ob2 + IPO. Current accepted post-SHA production objects with Source Parser and benchmark rebuilt. Identical UnitProXL physical fixture `build/publish-subsecond-01/fixture/project.json`. Two excluded warmup pairs, then 20 alternating measured pairs (44 calls). No builds, detailed instrumentation, filtering or hash work inside the timer.

| Median, ms | Control | Candidate |
|---|---:|---:|
| Full PUBLISH | 1309.825 | 1301.985 |
| Source | 422.0665 | 421.3865 |

| Paired statistic | Full PUBLISH | Source |
|---|---:|---:|
| Median gain, ms | **4.735** | **3.7345** |
| Mean gain, ms | 17.1505 | 6.57405 |
| Bootstrap mean 95% interval, ms | [1.8765, 33.8710] | [0.59995, 12.95865] |
| Faster pairs | 12/20 | 13/20 |

The mean PUBLISH result is substantially larger than its paired median because several reference calls are much slower. All observations are retained. No general 17 ms production speedup is claimed. Independent medians and median paired gains are different statistics.

Raw series and hashes: `build/source-type-prepare-01/results-20261023/`.

## Native candidate versus original frozen production

The native candidate has no A/B runtime selector (also verified in executable bytes). It is compared with the original frozen post-SHA production benchmark at `build/publish-critical-path-02/frozen/ServerEngineV4PublishBenchmark.exe`. Two excluded warmup pairs plus 12 measured alternating pairs (28 calls), same fixture/goldens. This is a separate two-executable check, not pooled with the same-executable series.

| Median, ms | Original production | Native candidate |
|---|---:|---:|
| Full PUBLISH | 1309.840 | 1309.845 |
| Source | 424.176 | 421.073 |

| Paired statistic | Full PUBLISH | Source |
|---|---:|---:|
| Median gain, ms | 12.150 | 1.326 |
| Mean gain, ms | 8.29917 | 4.33342 |
| Bootstrap mean 95% interval, ms | **[-2.34417, 18.40667]** | **[-3.09183, 12.68008]** |
| Faster pairs | 9/12 | 6/12 |

Both intervals include zero. Full-PUBLISH medians are effectively equal. The positive paired median alone is insufficient to establish a reliable production win. Bootstrap uses 10,000 paired resamples with a fixed seed in each series. No meaningful peak-working-set reduction is established; raw memory statistics are retained.

Raw bridge: `build/source-type-prepare-01/bridge-20261023/`.

## Correctness

- Release test configurations **5/5 pass**: reference, candidate, no experiment macro, native candidate and forced cache unavailability. Every frontend suite includes 22 Header V2 semantic contract fixtures.
- Tests cover 5000 named types to force cache replacements; CV, lvalue/rvalue references; caller-specific path prefixes; invalid/scalar-reference outputs; missing identity followed by catalog append; and independent resolver worker contexts.
- Seven negative programs compare exact first-error status, kind, physical file/range and detail with the cache on/off. These include scalar member access, reference members, array bounds, missing members and syntax errors after successful warmup accesses.
- Forced unavailability skips the single cache allocation after recording the attempt, exercising the same fallback behavior as caught allocation failure; it is test-only.
- All **78 full calls** (two initial audits + four candidate audits + 44 timing calls + 28 bridge calls) preserve compiled SHA-256 `a49506c8df1a999d68624f3a3fd0c317516015b7375deb992ced99736b75522d` and runtime SHA-256 `a0281b3bb48103a7cd339cccd072d24f3524db9932bc67b58a714906996b5ee6`, plus expected type/member counts. compiled.bin includes SourceMap. Hashes are verified outside timing.
- `git diff --check` and runner Python syntax compilation pass.

Keep the diagnostics and semantic checks. The experiment rules out treating either member-cache collisions or repeated type preparation as a demonstrated large Source bottleneck. Production behavior and the sub-second PUBLISH status are unchanged.
