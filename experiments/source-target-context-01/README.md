# SOURCE-TARGET-CONTEXT-01

Result: retained as an experiment. Native Source paired median gain 8.817 ms; full-PUBLISH paired median gain 1.875 ms with mean interval crossing zero. No production-default adoption. [Report](../../docs/SOURCE_TARGET_CONTEXT_01.md).

Isolated current-target context for Source V2. A 20-byte parser-owned record holds physical file, scope, name string_id, semantic WHO and root type_ref. No allocation, retained operation list, Graph pointer or type database is added.

On a matching current target, scope dispatch reuses WHO and endpoint setup reuses the root type. Existing punctuation lookahead still distinguishes assignments from declarations when a type and object share a spelling. Every endpoint resets path/location and publishes its current provenance. RHS resolution neither reads nor writes target context. Root changes, declaration entry and namespace entry/exit clear it; successful declarations/externs seed the new context.

Standalone `CW_SOURCE_TARGET_CONTEXT_AB` targets select `CW_SOURCE_TARGET_CONTEXT=0/1`. Counter-free timing and separate audit executables use the same algorithm. Native candidate targets define `CW_SOURCE_TARGET_CONTEXT_V2`, eliminating the runtime selector. Neither macro is enabled by root CMake or the production VS project at experiment preparation time. Other rejected experiments remain disabled.

Requires complete current accepted post-SHA production Release+IPO objects in `build/publish-critical-path-02/symbols/ServerEngineV4PublishBenchmark.dir/Release`. Source Parser, benchmark and test wrapper are rebuilt. Rebuild the baseline after source/configuration/ABI changes.

```powershell
cmake -S experiments/source-target-context-01 -B build/source-target-context-01 -A x64
cmake --build build/source-target-context-01 --config Release
ctest --test-dir build/source-target-context-01 -C Release --output-on-failure
python experiments/source-target-context-01/run.py build/source-target-context-01/audit-new --audit
python experiments/source-target-context-01/run.py build/source-target-context-01/results-new --pairs 20
```

Use fresh output directories. The audit performs two full calls per mode and records simple sequential-owner counts; its times are not speed evidence. Timing has no counters, uses two excluded warmup pairs followed by 20 alternating measured pairs, and verifies both artifact goldens outside the operation timer after every call. Generic CSV `workers=0/1` labels mean control/context, not changed scheduling.

For an independent native comparison against frozen original V2:

```powershell
python experiments/source-target-context-01/run.py build/source-target-context-01/bridge-new --pairs 12 --native --baseline build/publish-critical-path-02/frozen/ServerEngineV4PublishBenchmark.exe
```

Tests run the full frontend suite plus 22 Header V2 contract fixtures with reference, context, no experiment macro and native candidate. A dedicated fixture compares ordered Graph initializations/links, SourceMap contributions/ranges/dependencies/initialization targets and exact warnings across RHS use, namespace shadowing, root changes, externs, string overwrites and a type/object spelling collision after cache warmup. Ten negative programs compare the full committed prefix plus first-error status, kind, file, byte range and detail. The existing whole-object const-bool failure is preserved rather than hidden by the cache.

Performance scope is the validated UnitProXL fixture. Additional UnitProXL_V2/ABB Source locality was measured in SOURCE-LOCALITY-01, but their full production compatibility blockers remain; no speedup on them is claimed.
