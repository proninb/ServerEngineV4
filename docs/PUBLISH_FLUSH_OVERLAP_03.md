# Overlap compiled-image flush with physical planning

The later [PUBLISH policy](PUBLISH_POLICY_04.md) removes explicit flush from
PUBLISH. The overlap described here remains active for durable REBUILD; the
measurements below document the earlier PUBLISH implementation.

After semantic encoding, `compiled.bin` is immutable. Type/runtime planning
reads its const view, and all subsequent physical-column encoding writes to the
separate `runtime.bin` mapping. The compiled image can therefore flush on a third
execution lane while the existing two planning tasks run.

This path applies to compiled images at least 8 MiB on machines with at least
three execution lanes. Small images and machines with fewer lanes retain the
existing final synchronous flush of both files. The shared full-construction
path serves PUBLISH and REBUILD.

No flush is skipped. Planning joins all lanes and checks the compiled flush
result before proceeding. Any flush failure propagates through the existing
failure/cleanup path. The runtime image still flushes after all physical-column
writes, and resident publication follows successful persistence. Format,
semantics and output bytes are unchanged.

## Timing interpretation

`compiled_plan_ms` now includes the compiled-image flush when overlap is active.
The new `compiled_flush_overlap_ms` is a nested worker duration inside that
stage. `compiled_flush_ms` measures the final serial flush: runtime.bin only on
the overlap path, both files on the fallback path. Do not add the overlapping
worker duration to the top-level stage total.

## Performance

MSVC Windows x64 Release with IPO ON, outside the filesystem sandbox. The saved
baseline includes the preceding runtime-image and owned-snapshot changes. Both
executables use the same dedicated full UnitProXL fixture under
`build/publish-subsecond-01/fixture`; no concurrent builds ran during timing.

The first ordinary-PUBLISH alternating series showed a paired median saving of
29.640 ms, faster in 7/7 pairs. A five-pair repeat was inconclusive: -5.660 ms
paired median saving, faster in 2/5 pairs. Both series are retained in the raw
results; the second result prompted an order-controlled check.

The control series used one warmup block followed by eight measured blocks,
alternating ABBA and BAAB. Each block contains two baseline and two candidate
PUBLISH invocations in separate processes. The difference of within-block
means cancels linear timing drift within each block.

| Controlled normal PUBLISH | Result |
| --- | ---: |
| Baseline mean | 1533.440 ms |
| Candidate mean | 1493.276 ms |
| Mean within-block saving | **40.164 ms** |
| Student-t 95% interval across eight block savings | **23.304–57.023 ms** |
| Faster blocks | **8/8** |

This is a modest reduction, approximately 2.6% in this series. It does not achieve
sub-second PUBLISH. The interval describes these block samples, not a latency
guarantee under arbitrary system load.

Separate three-pair stage profiles measured persistence medians of
311.240 -> 283.537 ms. Final serial flush medians were 68.646 -> 21.030 ms;
the candidate's overlapping flush median was 49.746 ms. Plan wall time grew
89.587 -> 99.784 ms. These are separate nested/profile measurements and must
not be summed into the controlled normal-PUBLISH improvement. Total stage-series
time was noisy and did not improve consistently.

## Validation

All 72 timed/warmup artifact sets across the initial, repeat, profile and control
series matched both SHA-256 values and passed independent saved-baseline audits
outside the timer:

- compiled.bin: `A49506C8DF1A999D68624F3A3FD0C317516015B7375DEB992CED99736B75522D`
- runtime.bin: `A0281B3BB48103A7CD339CCCD072D24F3524DB9932BC67B58A714906996B5EE6`

The production server and all test targets built in Release. All 22 CTest suites
passed, using workspace-local TEMP/TMP and loopback access. Nine small integration
cases passed artifact parity/audits or matching failure diagnostics; these also
exercise the serial-flush fallback.

Full UnitProXL REBUILD passed byte parity for compiled.bin/runtime.bin between
baseline and candidate, with independent audits. Subsequent PUBLISH passed an
audit and removed BUILD acceleration files, leaving only compiled.bin/runtime.bin.

Raw scripts, all timing series, block-level differences, independent audits,
build logs and regression results are in `build/publish-flush-overlap-03/`.
The candidate executable SHA-256 is
`F825FFBEC696C0BFB808CD41E0637C26AE0D3CC73599A5325F317BCDC7476062`;
the saved baseline is
`4C194F9BD279BABAF578B029960E8198077A94EBFC27AC9CB75FB0B1833FA999`.
