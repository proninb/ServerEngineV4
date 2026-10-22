# Batched Source string writes (35)

Source V2 now collects resolved character initialization records in a reusable
parser-owned buffer. Graph validates the scalar type and record keys once per
batch and reserves index/vector capacity for the whole batch before applying
records in order. Individual and batched writes share the same upsert logic.
Existing initialization slots and last-write-wins behavior are retained.

Paths are still interned in character order. Source Map entries are published
in that order after the Graph batch; one duplicate-initializer warning is
emitted per replaced string statement. Successful Graph/provenance ordering,
canonical handles and persisted formats are unchanged. Allocation failure
can stop before the batch rather than after a character prefix; it remains
a fatal error and does not publish project artifacts.

`SERVER_ENGINE_SOURCE_STRING_BATCH_V2` defaults to ON. OFF retains the scalar
Source path for comparison. BUILD/REBUILD parsers are unchanged.

## Controlled full-operation measurement

Native MSVC Release with IPO, disposable UnitProXL fixture, no concurrent
builds or tests. One excluded warmup block plus eight mirrored ABBA/BAAB
blocks: 16 measured ordinary PUBLISH calls per variant. Every call checked
both golden SHA256 hashes and ran an independent baseline artifact audit.

| Metric | Result |
| --- | ---: |
| Baseline mean | 1412.454 ms |
| Batch mean | 1383.205 ms |
| Paired mean saving | 29.249 ms (2.07%) |
| Student-t 95% interval, 8 block differences | 11.310–47.189 ms |
| Winning paired blocks | 7/8 |

The improvement is established for this fixture and machine. Subsecond
PUBLISH is still not reached. Raw measurements, executables and analysis
are retained in build/publish-string-batch-35/.

## Validation

Graph tests cover repeated targets within a batch, stable replacement slots,
last-write ordering and invalid-batch validation before mutation. Frontend
tests exercise both legacy and V2 string parsing, including replacing a long
string with a shorter or empty string, zero-filled tails, escapes, literal
concatenation, inherited array paths, invalid literals and one warning per
replaced statement. Nine integration scenarios and thirteen actual parser
error comparisons matched baseline.

The complete candidate CTest suite passed: 22/22 tests. `git diff --check`
reported no whitespace errors.
