# SOURCE-TYPE-PREPARE-01

Result: no production adoption. Preparation hit rate is 99.02%, but the native bridge does not establish a reliable full-PUBLISH speedup. [Report](../../docs/SOURCE_TYPE_PREPARE_01.md).

Isolated Source V2 experiment: cache successful `type_ref -> record + dereference` preparation in each resolver. Existing member-transition lookup, syntax/error order, WHO, Graph writes, dependencies and SourceMap are unchanged. Record preparation still runs at the same point before consuming the member identifier.

The candidate uses 4096 direct-mapped 12-byte descriptors (48 KiB), allocated lazily. Full `type_ref` equality prevents collision errors. Only successful resolutions are stored; failures are never cached across catalog appends. A failed cache allocation falls back to the original resolver for the rest of that resolver lifetime. Every worker/resolver owns its state. No shared type database is introduced.

`CW_SOURCE_TYPE_PREPARE_AB` is opt-in for standalone targets only. `CW_SOURCE_TYPE_PREPARE=0/1` selects control/candidate in one executable. Production builds retain the original preparation path. Source string scratch reuse and Runtime pretouch/worker-cap experiments are disabled.

Requires complete current production post-SHA MSVC Release+IPO objects from `build/publish-critical-path-02/symbols/ServerEngineV4PublishBenchmark.dir/Release`. Rebuild that baseline after source/configuration/ABI changes. This project rebuilds Source Parser, benchmark and frontend test code; other objects are reused.

```powershell
cmake -S experiments/source-type-prepare-01 -B build/source-type-prepare-01 -A x64
cmake --build build/source-type-prepare-01 --config Release
ctest --test-dir build/source-type-prepare-01 -C Release --output-on-failure
python experiments/source-type-prepare-01/run.py build/source-type-prepare-01/audit-new --audit
python experiments/source-type-prepare-01/run.py build/source-type-prepare-01/results-new --pairs 20
```

Output directories must be new. Diagnostic `SourceTypePrepare01Audit` counts record inputs, unique type/member keys, existing member-cache hits/misses, preparation-cache hits, inheritance lookups and emitted path steps. Its exact-key sets add overhead: **do not use diagnostic timings for performance conclusions**. Thread-local counters are read for the Source owner thread; they are not a cross-thread/process aggregate. Full benchmark Source evaluation runs on that owner. Audit allocation failure is explicitly reported.

Counter-free `SourceTypePrepare01` runs two excluded warmup pairs then 20 alternating measured pairs, verifying artifact goldens after every call outside timing. Generic CSV `workers=0/1` means preparation control/candidate, not actual worker counts. Scheduling remains the same. Executable/source hashes and raw outputs are retained.

For a separate comparison without the A/B selector, `SourceTypePrepare01Native` and `SourceTypePrepare01NativeTests` define only `CW_SOURCE_TYPE_PREPARE_V2`. No root production option enables this macro. After building and testing, run:

```powershell
python experiments/source-type-prepare-01/run.py build/source-type-prepare-01/bridge-new --pairs 12 --native --baseline build/publish-critical-path-02/frozen/ServerEngineV4PublishBenchmark.exe
```

Tests run the frontend suite (including 22 Header V2 contract fixtures) with control, candidate, no experiment macro, and forced cache unavailability. Additional contracts exercise 5000 named inputs to force replacements, CV/reference variants, caller path prefixes, invalid output reset, negative lookup followed by catalog append, and seven exact first-error comparisons including physical origin. Forced unavailability skips cache allocation after marking it attempted; it exercises the same fallback behavior as a caught allocation failure.

The initial `audit-baseline-20261023` freezes the counter-only code before cache implementation. Candidate audit and timing/native series use later source versions; each manifest records its own executable and source hashes. The native candidate changes only compile-time activation and removes the runtime selector, using the same descriptor algorithm.
