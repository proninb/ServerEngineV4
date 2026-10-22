# Parallel Source identifier hashes

Experiment 20 computes identifier FNV-1a hashes during per-file parallel token
decoding. Workers read immutable file bytes and lexical words only. The serial
Source cursor still interns names in canonical token order, using the existing
`string_table::intern_prehashed` path. Full spelling equality, string IDs,
identity resolution, Graph writes, diagnostics and root-local provenance remain
in that serial path. Allocation or preparation failures fall back to the normal
cursor. No global string table or Graph mutation happens on worker threads.

The Source timer includes parallel preparation and destruction of token buffers.
The initial seven alternating paired ordinary runs averaged 1420.276 ms
baseline and 1414.601 ms candidate. Three separate instrumented pairs averaged:

| Stage | Baseline, ms | Candidate, ms |
|---|---:|---:|
| Full PUBLISH | 1413.453 | 1398.800 |
| Source including preparation/destruction | 474.636 | 452.103 |

One candidate stage run reported 8.244 ms setup, 429.991 ms replay and
443.498 ms Source total. These instrumented results cannot be combined with the
ordinary-run total difference. Acceptance uses a separate, predefined 16-block
alternating ABBA/BAAB series, with two runs per variant per block and an excluded
warmup block.

Measurements use Release + IPO, no PGO, native execution outside the filesystem
sandbox and no concurrent build/test activity. Both executables use the same
standard MSVC flags, including `/EHsc`. An earlier initial build accidentally
overrode those flags; it was rebuilt before any tests or performance runs.
Every full-fixture call compares both artifact SHA-256 hashes and runs an
independent baseline audit. Frontend regressions and nine integration scenarios
passed before the controlled series.

Raw scripts, binaries, logs and source snapshots are in
`build/publish-source-prehash-20/`.

## Acceptance result

All 16 predefined alternating ABBA/BAAB blocks are retained, including outliers:

| Metric | Result |
|---|---:|
| Baseline mean full PUBLISH | 1405.311 ms |
| Candidate mean full PUBLISH | 1396.905 ms |
| Mean paired block saving | 8.406 ms (0.60%) |
| Student-t 95% interval | [1.951, 14.861] ms |
| Blocks favoring candidate | 12 / 16 |

All 92 full-fixture calls, including warmups and the initial stage series,
matched both reference artifact hashes and passed independent baseline audits.
The feature is enabled by default with
`SERVER_ENGINE_SOURCE_PREHASH_PARALLEL_V2`; OFF retains the direct cursor.
Prepared tokens use 16 bytes each (20 when combined with the separate optional
prebind experiment), so this trades temporary token-buffer memory for a small
measured reduction in full PUBLISH time. Preparation failure still falls back.

The preserved measured binaries have SHA-256:

- Baseline: `08B23C0A6695D1EE7216D0529AE43890A2152E9E5E9225D577872C3F559EAB38`.
- Candidate: `2039AC35679CAF4798F96B069735617BE53CDDBED58BD0D004CDD5649A15E211`.

The measured candidate used the same macro through CMAKE_CXX_FLAGS. The final
integration defines it through the dedicated CMake option for the server,
benchmark and tests. No new PGO result is claimed; that profile must be retrained
for the changed sources. Subsecond full PUBLISH remains unachieved.

Final integrated default build: all 22 CTest tests passed. A fresh full-fixture
PUBLISH matched both reference artifact SHA-256 hashes, passed an independent
baseline audit and left only `compiled.bin` and `runtime.bin` in its output
directory. Its single 1488.62 ms check is not a controlled performance result.
