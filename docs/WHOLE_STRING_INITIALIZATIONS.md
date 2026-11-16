# Whole-string initializations

Implemented 2026-11-15, following COMPACT_CHAR_INITIALIZATIONS.md.

Each bounded narrow-char array now has one final Source initialization. The
construction value contains an interned binary literal, its destination extent
and an optional bitmap for partially assigned arrays. Decoded literals share
the existing string table. Trailing zeros are omitted from the literal.

Both Source parsers use this representation. A string replaces the complete
array value. A scalar character edit updates that same value and its bitmap,
preserving last-write order and untouched constructor bytes. Source provenance
uses the complete array endpoint, including when several roots edit it.

The runtime file has a separate immutable initialization-byte pool. A fixed-size
descriptor identifies a literal and an optional bitmap in that pool. Full arrays
execute one memcpy for the literal and one memset for the remaining zero tail.
Partial arrays combine adjacent selected bytes into copy/fill ranges. The zero
tail occupies no bytes in the pool. An empty string needs only its descriptor.

Compiled images with this representation use format 30. Runtime format is 3,
with 33 directory entries and section 66 (runtime_initialization_bytes).
Regenerate older artifacts with PUBLISH using the new executable. The source
project.json format and physical SHM layout are unchanged.

## Verification

- Frontend regression tests pass for both Source parsers.
- Compiled-project tests cover whole-array persistence, an embedded NUL, a
  4097-byte destination with a three-byte literal, explicit clearing of poisoned
  tail bytes, independent/reused runtime sizing, persisted runtime reconstruction,
  invalid bitmap bounds and corrupted persisted literal lengths.
- Real TCP PUBLISH, LOAD, BUILD and fresh-process LOAD match the old executable's
  complete native SHM bytes. Fixtures include two Source roots editing one array,
  removal of an edit, a long empty string, embedded NUL and reference paths.
- The complete UnitProTr SHM matches the preceding eight-byte implementation
  after PUBLISH and three fresh LOADs. The 72-byte mutable system header is
  excluded. SHA-256:
  `3b0ba669c973ab6ea31b93e4de0b0d69750627a6b5248f2a6af699c89d50a88f`.

| Metric | Eight-byte blocks | Whole strings |
|---|---:|---:|
| Initialization records | 32,348,014 | 12,259,964 |
| Working compiled.bin | 3,852,965,376 B | 3,088,998,656 B |
| runtime.bin | 2,286,644,544 B | 1,659,952,362 B |
| Combined files | 6,139,609,920 B | 4,748,951,018 B |
| PUBLISH to confirmed LOADED | 51.733 s | 44.569 s |
| Three fresh-process LOADs | 1.798–1.807 s | 1.592–1.615 s |
| Physical SHM | 1,006,509,016 B | unchanged |

The entire runtime initialization-byte pool is 16,130,410 bytes. The remaining
file space includes non-string initializations, 9.49 million objects, 20.23
million links and their semantic/physical indexes.

Timings include process startup with warm filesystem caches: one PUBLISH sample
per implementation, three LOAD samples. The isolated benchmark compiled.bin is
64 bytes larger due to source-path metadata; the table uses working-file sizes.

Evidence:

- build/unitprotr-whole-string-check-v2/summary.json
- build/unitprotr-whole-string-full-20261115/summary.json
- build/unitprotr-whole-string-full-20261115/sizes.json

Working artifacts in examples/UnitProTr were regenerated. Its server returned
STATE LOADED and DIO_Data.LEN=500 using:

```powershell
build/unitprotr-whole-string-bin/ServerEngineV4.exe build/unitprotr-whole-string-working-run/server.json
```
