# PUBLISH verified snapshot ownership

## Change

PUBLISH enables owned content before configuration composition. Initial source
preparation and Header V2 include preparation explicitly call `adopt_acquire()`:
it transfers a successfully verified snapshot's string allocation into File
Context instead of copying it into the growing shared content arena. A separate
heap-allocated string object keeps even short-string views stable while file
discovery grows the file and ownership tables.

Ordinary `apply_acquire()` remains non-consuming. Configuration readers rely on
reading `result.snapshot.bytes` after publishing its physical state, so changing
that operation implicitly would break configuration parsing. BUILD and REBUILD
retain their existing arena publication. The original aggregate 32-bit content
size limit includes both owned buffers and arena bytes.

The shared Windows snapshot/proof reader also queries USN through its existing
read handle and already checked file information. It no longer opens the same
path again and repeats `GetFileInformationByHandle` just to capture the token.
Standalone path-based token queries still open and inspect the requested file.

SHA-256, full byte-count checks, the read handle's write/delete exclusion,
before/after metadata checks, timestamp observation and USN validation remain.
This is different from the historical fast-acquisition experiment that omitted
hashes and physical change checks. No persisted format, semantic order or flush
policy changes are involved.

## Profiling and measurement environment

The filesystem sandbox significantly changes these file-heavy measurements.
One diagnostic comparison using the same saved baseline executable and fixture
reported 2410.70 ms inside the sandbox and 1508.72 ms outside; lexical time was
873.291 versus 259.492 ms. These single runs explain why old sandbox absolute
timings must not be treated as deployment latency. All acceptance measurements
below ran outside the sandbox, for both variants.

Temporary coarse clocks measured approximately 51–54 ms publishing acquired
buffers in the baseline and 0.48–0.53 ms after ownership transfer. The clocks
were removed before the acceptance build. Diagnostic profiles are saved under
`build/publish-acquisition-02/` and are not the acceptance timing series.

## Acceptance results

MSVC Windows x64 Release with IPO ON. The saved baseline includes the preceding
runtime image reuse optimization on top of `d0e8c72`; the candidate adds only
this snapshot work. Both use the same dedicated UnitProXL copy at
`build/publish-subsecond-01/fixture/project.json`.

Normal `publish` mode: one warmup pair and seven measured alternating pairs in
separate processes, warm filesystem cache, no concurrent builds. Checks and
independent baseline audits run outside the lifecycle timer.

| Normal PUBLISH metric | Baseline median | Candidate median |
| --- | ---: | ---: |
| Total | 1520.960 ms | 1460.140 ms |
| Peak working set | 2559524864 bytes | 2574630912 bytes |

The paired median total saving is **64.440 ms**, with the candidate faster in
**7/7 pairs**. The memory tradeoff is approximately **14.4 MiB** more peak working
set in this series (about 0.6%). Sub-second PUBLISH is not achieved.

A separate `publish-stage` series (one warmup pair and three measured pairs)
reported lexical medians of **275.624 -> 174.309 ms**. Its total medians were
1559.480 -> 1461.530 ms. Do not add stage-series differences to normal-PUBLISH
gains or compare instrumentation series as if they were the same samples.

All 24 artifact sets, including warmups and stage profiles, matched both hashes:

- compiled.bin: `A49506C8DF1A999D68624F3A3FD0C317516015B7375DEB992CED99736B75522D`
- runtime.bin: `A0281B3BB48103A7CD339CCCD072D24F3524DB9932BC67B58A714906996B5EE6`

The baseline executable independently audited every artifact set. Baseline and
candidate executable SHA-256 values are respectively:

- `480CBDCDFA4CF04B2D445797DEBBD4B10C874516913924F47B48D198E6D86C36`
- `4C194F9BD279BABAF578B029960E8198077A94EBFC27AC9CB75FB0B1833FA999`

Raw measurements, runner, per-run outputs and audit logs are under
`build/publish-acquisition-02/`.

## Validation

All 22 Release CTest suites passed with workspace-local TEMP/TMP and loopback
access. New regressions cover empty, embedded-NUL, short and large snapshots;
zero-copy heap-buffer transfer; view stability through 257 appended file
identities; preservation of hashes and tokens; duplicate and changed reads;
missing and failed reads; non-consuming configuration publication; and owned
parallel physical/lexical preparation.

Snapshot and streaming proof tests compare bytes, hashes, observations and
available tokens against independent path-based token capture. Missing-file
classification is checked for both APIs.

Nine integration cases passed: macro branches, namespaces, cycles/aliases,
declaration fragments, pragma-once order, macro mutation, a missing active
include, simple and mixed workloads. Successful cases matched both artifact
hashes and passed independent baseline audits. The missing-include case retained
identical diagnostics and left no compiled artifact for both executables.
