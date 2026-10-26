# PUBLISH-FAST-ACQUIRE-02 — implementation and measurements

**Execution-mode correction:** the 1928.16 → 1659.42 ms series below ran inside
the default tool sandbox. It establishes a sandbox-specific benefit, not an
ordinary production speedup of 13.9%. Follow-up unsandboxed same-fixture A/B/C:
historical 1354.65 ms, modern reference 1354.13 ms, fast 1313.58 ms (about 3%).
B→C paired gain 36.70 ms, CI 34.93–62.29 ms, wins 13/15. See
`docs/PUBLISH_UNITPROXL_ABC_01.md` for controls and limitations.

2026-10-26. Production fresh PUBLISH now selects byte-only physical file
acquisition in File Context. On Windows this is CreateFileW / GetFileSizeEx /
ReadFile, without GetFileInformationByHandle before/after or last_write_time.
Normal PUBLISH still omits SHA-256 and USN and does not force durable flush.

Unlike the older `a13bda9` implementation, this change retains FILE_SHARE_READ:
an active writer causes changed_during_read, rather than permitting concurrent
write/delete sharing. BUILD and REBUILD keep their existing verified acquisition.
Configuration-manifest reads outside File Context are unchanged. Non-Windows
uses the previous stable snapshot reader without digest/token capture, since
portable streams do not offer Windows' sharing contract. No file format changes.

## Full UnitProXL results

MSVC 19.51, x64, Release + IPO, PGO OFF. One executable with a private reference
switch; only acquisition selection differs. Immutable identical input:
24,027 types / 137,584 objects / 367,144 links. Fifteen alternating native pairs
after warmup; all samples retained. Native timers finish before Project teardown.

| Native metric | Metadata-check reference | Fast byte acquisition |
|---|---:|---:|
| PUBLISH median | 1928.16 ms | 1659.42 ms |
| Process-lifetime peak working set median | 2,570,903,552 B | 2,571,554,816 B |

The difference of marginal total medians is 268.74 ms (13.94%). The median of
**paired** reference-minus-fast differences is **291.04 ms**, bootstrap 95% CI
**217.59–299.21 ms**; fast wins **15/15**. These are distinct statistics.
The working-set change is +651,264 B; there is no substantial memory benefit.

Separate seven-pair phase-only profile series:

| Profile median | Reference | Fast |
|---|---:|---:|
| Total PUBLISH | 1810.78 ms | 1574.48 ms |
| File acquisition / lexical preparation | 579.130 ms | 304.944 ms |
| Semantic | 774.700 ms | 814.083 ms |
| Persistence | 199.414 ms | 197.991 ms |
| Resident | 192.674 ms | 196.789 ms |

Profile paired total gain: 242.70 ms, CI 215.31–340.70 ms, wins 7/7. The profile
series is separate and later than native; phase medians are diagnostic and do
not add to an exact decomposition of native total differences. The observed
benefit is consistent with removing metadata I/O. This does not establish the
cause of the entire historical 1356.49-to-1840.59 ms cross-series difference or
reproduce the historical 1356.49 ms result.

## Correctness and scope

- `source_save`, `frontend_regressions`, `compiled_project`: 3/3 CTest suites pass
  in the frozen source build.
- 42 end-to-end fixture operations pass, with compiled/runtime bytes and logical
  resident SHM SHA-256 parity across reference/fast. Includes inheritance,
  constructors, nested values, links, late binding, malformed input, reserved
  names, reference cycles, extern mismatch, and BUILD/REBUILD on a compatible
  legacy fixture.
- 24 additional synthetic/full-corpus checks include PUBLISH, audit, LOAD and
  SHM parity before/after timing. Every one of the 44 timed observations also
  matches the same full compiled.bin and runtime.bin hashes.
- Direct acquisition tests cover binary bytes, empty/missing files, directories,
  active-writer rejection and retry after closing the writer. Normal File Context
  acquisition still calculates the expected content digest.
- A final production-source `source_save` build adds an explicit check of the
  optional digest control on the fast reader; see `production-ctest.log`.

The generic REBUILD parser does not support the enum fixture used for PUBLISH;
the initial smoke attempt hit this existing restriction. The final harness uses
a compatible legacy fixture for BUILD/REBUILD and retains the enum PUBLISH tests.

The production working tree has no runtime A/B environment switch. Private
controls and SHM hashing exist only in the generated experimental snapshot.
Hashing occurs outside operation timing; no builds/tests ran concurrently with
the timed series. Source, input and executable hashes are unchanged across the
measurement series. Existing unrelated CMake PGO and endpoint-resolver edits
were preserved.

## Retained evidence

`build/publish-fast-acquire-02/` contains source snapshot, source/input manifests,
binary, raw stdout/stderr, `samples.json`, `summary.json`, `checks.json`,
`parity.json`, `verified.json`, build/CTest and fixture logs.

Frozen measurement executable SHA-256:
`1a356c04c7d0458a35c1be877b5374f120417d79a663ecf4fd347c105ec448ab`.

Reproduction: `experiments/publish-fast-acquire-02/README.md`.
