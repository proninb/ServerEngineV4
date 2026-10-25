# SOURCE-LOCALITY-01

Diagnostic audit of sequential target reuse and page working sets. Production parser algorithms, scheduling and Graph commit order are unchanged. [Results](../../docs/SOURCE_LOCALITY_01.md).

`CW_SOURCE_LOCALITY_AUDIT` records actual ordered Source root boundaries, object declarations/externs, namespace barriers, typed assignment targets/operands and successful Graph writes. String statements count once as assignments; actual per-character Graph writes are recorded separately. Data stays with the sequential Source owner and is dumped after the lifecycle timer. The dump also retains partial prefixes on failed inputs. Diagnostic allocation and dump overhead is substantial; **no diagnostic time is a production performance measurement**.

Build requires complete current production Release+IPO objects in `build/publish-critical-path-02/symbols/ServerEngineV4PublishBenchmark.dir/Release`. Source Parser and benchmark are rebuilt, other objects reused. No rejected cache, scratch, Object-lane or pretouch macro is enabled.

```powershell
cmake -S experiments/source-locality-01 -B build/source-locality-01 -A x64
cmake --build build/source-locality-01 --config Release
python experiments/source-locality-01/test_analysis.py
python experiments/source-locality-01/prepare.py
python experiments/source-locality-01/run.py build/source-locality-01/capture-new
python experiments/source-locality-01/analyze.py build/source-locality-01/capture-new/unitproxl/capture-0
```

Use fresh output/fixture directories. `prepare.py` reads the requested UnitProXL_V2 and ABB originals, writes under build only, removes existing legacy Studio annotation macros from copied headers using the established helper, and supplies the empty SDK SimObject/SimLink and byte alias in a separate fixture header. Source leaf bytes remain unchanged. `preparation.json` and `source-transfer.json` record conversions and hashes. The shared importer now has optional `expand_chapters=True` / `--expand-chapters`, treating Source `.chap` include containers like `.book`, preserving include order and non-include fragments. Its default remains unchanged.

The control determines each fixture's own type count and artifact goldens. Successful diagnostic fixtures are run twice with exact artifact parity and identical event CSVs. Failures retain errors/prefixes and are **not** marked as passing WHO/G validation. Current blockers: UnitProXL_V2 Header enum U_TYPE_ENUM; ABB whole-object const-bool assignment on its fourteenth Source root.

Source-only structural scans remain useful for those unsupported corpora:

```powershell
python experiments/source-locality-01/structural.py build/publish-subsecond-01/fixture/project.json build/source-locality-01/structural-unitproxl-new
python experiments/source-locality-01/structural.py build/source-locality-01/fixtures/unitproxl-v2/project.json build/source-locality-01/structural-v2-new
python experiments/source-locality-01/structural.py build/source-locality-01/fixtures/abb/project.json build/source-locality-01/structural-abb-new
python experiments/source-locality-01/compare.py build/source-locality-01/capture-new/unitproxl/capture-0 build/source-locality-01/structural-unitproxl-new build/source-locality-01/structural-check-new.json
```

The structural scanner uses ordered Source names and namespace scopes, not Header types or canonical semantic WHO. All unclassified statements and unresolved operands are reported. It does not expand string writes or detect overlapping array prefixes/reference aliases. For UnitProXL its complete declaration/assignment identity stream matches native G with a bijection of object IDs; type IDs, source offsets and per-character writes are excluded from that comparison. This validates the locality measurement on that corpus, not full semantic support of the other inputs.

`analyze.py` outputs summaries per Source root and physical token file, run-length distributions, conservative reusable-target opportunities, declaration-seeded opportunities, cross-page operands, and writes shared across pages. An assignment reference is a symbolic link, not evidence of a value-evaluation dependency. Common last-object cache simulation includes both operand sides; dedicated-target reuse counts only left-hand assignments. They have different denominators.
