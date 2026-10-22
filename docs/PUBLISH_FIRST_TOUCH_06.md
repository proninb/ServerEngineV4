# Parallel first-touch of fresh compiled images

Large fresh compiled.bin mappings are zeroed in disjoint 64 KiB ranges before
the existing serial semantic encoder runs. At least 4 MiB is assigned per lane,
with at most 16 lanes. Images below 8 MiB and single-lane machines skip this
optional preparation. Workers join before encoding begins.

This moves demand-zero/page-allocation work away from the serial encoder.
It adds a complete write pass, so encoding time alone is not an acceptance
metric: total persistence and complete PUBLISH must include the preparation.
`compiled_map_ms` now includes this first-touch phase.

Worker creation or dispatch failure does not fail construction. A fresh mapping
is logically zero already; any partial first-touch is harmless, and the unchanged
encoder still initializes payload, padding and index cells. The final format,
Graph, semantic order and error handling are unchanged.

The shared full-construction path applies to PUBLISH and REBUILD. PUBLISH still
uses no USN/change tokens or explicit flush, as specified in PUBLISH_POLICY_04.
REBUILD/BUILD retain their existing change tracking and durability policy.

## Measurements

MSVC Windows x64 Release + IPO ON, outside the filesystem sandbox. Both binaries
use the same dedicated full UnitProXL fixture under
`build/publish-subsecond-01/fixture`. The baseline includes steps 01–04, without
the rejected static-path cache. No concurrent builds ran during timing.

The initial seven alternating ordinary-PUBLISH pairs showed 36.340 ms paired
median saving, faster in 7/7 pairs. Two independent order-controlled series then
used one warmup block and eight measured ABBA/BAAB blocks each. The difference
of the two baseline and two candidate means within each block controls linear
timing drift.

| Controlled series | Mean block saving | Faster blocks |
| --- | ---: | ---: |
| First eight blocks | 11.328 ms | 5/8 |
| Repeat eight blocks | 20.631 ms | 7/8 |
| Combined sixteen blocks | **15.979 ms** | **12/16** |

The combined Student-t 95% interval over block differences is **8.166–23.793 ms**.
Controlled overall means are **1429.200 -> 1413.221 ms**, approximately 1.1% less
time. This is a small improvement; sub-second PUBLISH is not achieved. The
interval describes these samples rather than guaranteeing deployment latency.

Separate three-pair stage profiles measured:

| Stage median | Baseline | Candidate |
| --- | ---: | ---: |
| Mapping plus first-touch | 0.214 ms | 12.853 ms |
| Semantic image encoding | 96.516 ms | 60.392 ms |
| Complete persistence | 227.860 ms | 199.020 ms |

These profile medians are not additive predictions of the normal-PUBLISH gain.
All total timings, including a high candidate total in the first stage profile,
remain in the raw data; no observations were filtered.

## Validation

All 96 artifact sets across normal/profile/control runs, including warmups,
matched the saved baseline hashes for both files and passed independent baseline
audits outside the timer:

- compiled.bin: `A49506C8DF1A999D68624F3A3FD0C317516015B7375DEB992CED99736B75522D`
- runtime.bin: `A0281B3BB48103A7CD339CCCD072D24F3524DB9932BC67B58A714906996B5EE6`

The server and all test targets built in Release, and all 22 CTest suites passed.
Nine small integration fixtures passed artifact parity/audits or matching failure
diagnostics. Full UnitProXL REBUILD and no-change BUILD passed byte parity and
independent audits; final PUBLISH removed BUILD acceleration artifacts.

Raw runs, scripts, block differences, audits and test/build logs are in
`build/publish-first-touch-06/`. Executable SHA-256:

- baseline: `87A76186B08A6BB4F66ACE0376B48D2C31E2F9ED31E60DFA2EF5889C88E9919E`
- candidate: `281D8121E4E7EEA2925A35483A456963303C50E84FF7291A02DCD6569B02AFB1`

## Rejected path-cache trial

A preceding lane-local static endpoint-path cache reduced initialization
planning from approximately 54 to 33 ms. Parallel type planning remained the
critical path, and ordinary PUBLISH showed only 1.650 ms paired median saving,
faster in 4/7 pairs. That cache was removed. Its results are retained under
`build/publish-static-paths-05/`; no production cache or feature flag remains.
