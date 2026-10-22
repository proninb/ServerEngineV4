# Runtime image preparation reuse

Full construction already prepares a validated `shm_runtime_v2` physical plan
before laying out `runtime.bin`. Previously, runtime layout sizing traversed
every Graph link and initialization endpoint again to count dereference programs.
Full construction now counts the compact owned physical plan instead. Link slot
and initialization counts must match the compiled project. The caller must supply
the successful, unchanged plan for that exact project; the default null argument
retains independent Graph-based sizing for other callers.

Large runtime images are cleared in disjoint 64 KiB blocks using existing
execution lanes, with at least 4 MiB per lane and at most 16 lanes. Small images
remain serial. Worker creation/dispatch failure joins the workers and falls back
to a complete serial clear. All bytes, including padding, remain initialized;
the image format and durable flush are unchanged.

The shared full-construction path applies to PUBLISH and REBUILD. The generic
runtime encoder also benefits when other callers create large images. LOAD,
semantic ordering and Graph writes are unchanged.

## Measurement

Windows x64, MSVC Release with IPO ON, baseline built from `d0e8c72` immediately
before these changes. One warmup pair and seven measured alternating pairs, in
separate processes. A dedicated copy of the full UnitProXL fixture lives under
`build/publish-subsecond-01/fixture`. Both executables use the same absolute
fixture path. `publish-stage` disables detailed per-token Source timers.

| Metric | Baseline median | Candidate median |
| --- | ---: | ---: |
| Complete PUBLISH | 2287.300 ms | 2177.240 ms |
| Persistence | 324.415 ms | 282.451 ms |
| Runtime layout + image initialization (`compiled_remap_ms`) | 62.554 ms | 8.171 ms |

The paired median complete-PUBLISH saving is **48.130 ms**, with the candidate
faster in six of seven pairs. The difference of total medians is larger and
should not be treated as the isolated optimization gain: lexical preparation
also varies (baseline median 908.770 ms, candidate 864.945 ms).

These measurements do **not** demonstrate sub-second PUBLISH. They also are not
directly comparable to historical measurements on a differently located fixture.
Lexical preparation alone takes approximately 0.9 seconds in this series;
file acquisition was not timed separately from the rest of that stage.
These runs executed inside the filesystem sandbox. Subsequent native measurements
in [PUBLISH_OWNED_SNAPSHOTS_02.md](PUBLISH_OWNED_SNAPSHOTS_02.md) isolated substantial
sandbox overhead; use the later native A/B series for current absolute latency.

All 16 measured/warmup artifacts matched the baseline SHA-256 for both files:

- compiled.bin: `A49506C8DF1A999D68624F3A3FD0C317516015B7375DEB992CED99736B75522D`
- runtime.bin: `A0281B3BB48103A7CD339CCCD072D24F3524DB9932BC67B58A714906996B5EE6`

The preserved baseline executable independently audited each resulting artifact
set outside the PUBLISH timer. Raw rows, per-run profiles, audit outputs and the
measurement script are under `build/publish-subsecond-01/`.

## Regression coverage

Compiled-project tests cover poisoned small and large output buffers, uneven
parallel tails, guard bytes, complete payload/padding initialization, and
byte-identical encoding independent of prior buffer contents. Another regression
compares Graph-based and physical-plan-based runtime directories for direct and
dereferenced link/initialization endpoints and rejects mismatched plan counts.

All 22 Release CTest suites passed. Tests used a workspace-local TEMP/TMP and
ran with local TCP access: the sandbox otherwise prevents the communication
regression test from opening its loopback connections. Synthetic simple
(1,000 types, no links) and mixed (501 types) fixtures also passed both-artifact
SHA parity and independent baseline audits.
