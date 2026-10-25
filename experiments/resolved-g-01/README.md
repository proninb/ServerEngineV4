# RESOLVED-G-01

Standalone diagnostic benchmark for **resolved Source operations → existing Header G**.
It does not change the production algorithm or enable hooks in the root production targets.

## Build and verify

```powershell
cmake -S experiments/resolved-g-01 -B build/resolved-g-01 -G "Visual Studio 18 2026" -A x64
cmake --build build/resolved-g-01 --config Release --parallel 8 --target ResolvedGReplay01
ctest --test-dir build/resolved-g-01 -C Release --output-on-failure
python experiments/resolved-g-01/run.py build/resolved-g-01/measurements --pairs 10 --replays 15
```

The driver defaults to the existing UnitProXL fixture and `build/enum-v4/Release/ServerEngineV4PublishBenchmark.exe` as the uninstrumented control. Override `--baseline`, `--exe`, `--project` and `--types` as needed. Build the selected baseline from the same source/configuration. The experiment always recompiles its complete benchmark target from source: no old Graph objects are imported.

## Measured boundary

1. At Source entry, snapshot the complete owning Header G. Preserve size and capacity of its 20 main vectors and all index counters. Copy the immutable constructor-default table's values; Source does not mutate that table.
2. During one successful ordinary Source parse, capture the ordered input and return handles/replacement flags of seven resolved mutators: derive, object creation, extern completion, path interning, scalar initialization, batch initialization and link rebinding. Capture is single-threaded, matching the existing Source mutation path.
3. Stop recording. Clone the Header snapshot before each replay, outside its timer.
4. Execute only the immutable operation tape and resolved G APIs. There is no Lexer, Parser, SourceMap, string interning or name binding in this loop. Existing Graph checks, indexes, path hashing/deduplication, allocations and vector growth remain unchanged.
5. Stop the timer before exact final-data/index comparison and Graph destruction. The first two warmups additionally check every returned handle and replacement flag. Measured runs perform the same mutations without these per-call comparison branches, then check the whole final G.

The replay includes tape traversal/dispatch and normal API status checks. It is **not an exclusive CPU profile of Graph functions**. The Header graph is already built; this does not measure all Header-to-G construction.

Internal equality covers all semantic arrays, all index fields/counters and constructor defaults. The four bytes of C++ tail padding in `link_target_index_slot` are deliberately excluded; its `key` and `link` are compared exactly. Padding is not semantic or persisted state.

## Controls and limitations

`CW_RESOLVED_G_01` is defined only on the standalone benchmark target. Without that define all hooks/friend access disappear. With `CW_RESOLVED_G_CAPTURE=0` the same benchmark runs ordinary PUBLISH; the driver alternates it with the uninstrumented executable to calibrate disabled-hook overhead.

`CW_RESOLVED_G_CAPTURE=1` captures and replays; `CW_RESOLVED_G_REPEATS` selects measured repetitions, after two warmups. **Do not interpret this process's Source or total PUBLISH timers as production performance:** they include capture, replay, verification and output.

`CW_RESOLVED_G_CORRUPT=1` is a negative-test switch: it changes a final initializer in the captured tape and must fail final-state parity.

The driver checks compiled.bin/runtime.bin SHA-256 after every successful control/capture process, outside all measured intervals. The tiny fixture covers repeated writes, reference replacement, repeated paths, Source-created array types, extern completion and string batches. It also proves that deliberate tape corruption is detected.

The 96-byte event tape is diagnostic input, not a proposed production IR. Tracked capacity figures cover the main Graph/tape vectors, not allocator metadata, cold constructor-default capacity or total process peak memory.

Do not subtract replay time from Source time to claim a cost for Parser or SourceMap: allocator state, cache state and the resolved input layout differ. No pre-reservation candidate, new identity database, dispatcher/window or new production Source pipeline is introduced.
