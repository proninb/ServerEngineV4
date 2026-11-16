# Compact char-array initialization

This describes the first optimization. Whole-array literals and zero-tail
operations now replace eight-byte Source buckets; see WHOLE_STRING_INITIALIZATIONS.md.

Source string assignments previously emitted one Graph initialization per byte
of the complete destination array, including its zero-filled tail. This produced
191,863,650 initializations for UnitProTr.

Both Source parsers now emit aligned eight-byte buckets. `byte_patch` contains
eight byte values and a write mask in the existing 16-byte construction value.
Scalar char-array writes use the same buckets. Graph updates merge masks and
overwrite only the selected bytes, preserving statement order and untouched
constructor values. The last bucket is restricted to the array's actual bound.
Zero-filled tails remain explicit; dropping them would change constructor and
repeated-assignment semantics.

The compiled image validates the final array boundary. Fixed Direct and Runtime
V2 apply the masks. Persisted Runtime V2 uses the first formerly reserved byte
as its optional mask; zero retains the ordinary scalar-store representation.

Images containing patches use compiled format 29; runtime format is now 2.
Old executables cannot consume these files. Regenerate existing runtime files
with PUBLISH using the new executable; source project.json is unchanged.

## Verification, 2026-11-15

- Frontend regression tests pass for both Source parsers, including repeated
  strings, empty strings, scalar/string overwrites and partial final buckets.
- Compiled-project tests pass, including rejection of an out-of-array mask.
- Real TCP PUBLISH, LOAD, BUILD and fresh-process LOAD produce identical native
  SHM bytes to the previous executable. BUILD tests include two Source roots
  writing different bytes in the same bucket and removal of a scalar write.
- Full UnitProTr SHM matches the previous executable byte for byte after
  excluding the 72-byte mutable runtime-system header. SHA-256:
  `3b0ba669c973ab6ea31b93e4de0b0d69750627a6b5248f2a6af699c89d50a88f`.

| Metric | Previous | Compact |
|---|---:|---:|
| Initializations | 191,863,650 | 32,348,014 |
| compiled.bin, working project | 11,531,616,896 B | 3,852,965,376 B |
| runtime.bin | 7,391,191,488 B | 2,286,644,544 B |
| Combined files | 18,922,808,384 B | 6,139,609,920 B |
| PUBLISH to confirmed LOADED | 138.844 s | 51.733 s |
| New-process LOAD, three runs | 3.501–5.592 s | 1.798–1.807 s |
| Physical SHM layout | 1,006,509,016 B | unchanged |

Timings include process startup with filesystem caches warm. This is one
PUBLISH sample per executable, not a controlled performance distribution.
The isolated benchmark compiled.bin is 64 bytes larger because source path
metadata differs; the table uses the regenerated working-project file size.

Evidence:

- build/unitprotr-compact-string-check-v5/summary.json
- build/unitprotr-compact-full-20261115/summary.json
- build/unitprotr-compact-full-20261115/sizes.json

Executable: build/unitprotr-compact-bin/ServerEngineV4.exe.
The working examples/UnitProTr files were regenerated and its console server
reported STATE LOADED using build/unitprotr-compact-working-run/server.json.

Remaining file space includes 9.49 million objects, 20.23 million links,
semantic/query indexes and physical link records. This change specifically
removes the dominant per-character initialization overhead.
