# PLS10-C15 — Endpoint Preparation Structural Audit

**NO ADOPTION of the tested declaration-WHO candidate.**

Extraction already supplies local name/symbol IDs and typed selectors. Global
resolution must assign WHO and walk current Header/member metadata. C15 found
one provable repeated lookup: declare() already receives WHO from
identities.resolve(), while apply() looked it up again through symbol_entity()
for provenance. The private candidate returns the successful WHO and passes it
on. No new cache/index, Graph API/ABI, lifetime or temporal-reference changes.
Failure paths throw before the WHO is consumed, preserving diagnostics/order.

`PLS10_C15_DIRECT=0/1` selects the original extra lookup or direct return in the
same binary. C10 endpoint scratch stays on. Audit checks the returned identity's
kind, parent, name and declaration-symbol invariants without another find().
Native counters and checks are compiled out. C10 sources remain untouched.

```powershell
cmake -S experiments/parallel-local-source-10-c15 -B build/parallel-local-source-10-c15 -G "Visual Studio 18 2026" -A x64 -DPLS10_CORE_LIBRARY=C:/Boris/ServerEngineV4/build/parallel-local-source-10-c4/base/Release/PLS08Core.lib
cmake --build build/parallel-local-source-10-c15 --config Release --parallel 4
python experiments/parallel-local-source-10-c15/test.py build/parallel-local-source-10-c15/Release/PublishDeclarationWho10C15.exe build/parallel-local-source-10-c15/Release/DeclarationWhoAudit10C15.exe build/parallel-local-source-10-c15/fixtures
python experiments/parallel-local-source-10-c15/run.py build/parallel-local-source-10-c15/ab-01
```

The frozen 15-block four-arm runner compares V2, original C10, off and on, with
no concurrent builds/tests. It retains all samples, hashes persisted output
after each operation and cold-checks complete native storage outside timing.
Labels `off` and `reuse` mean extra lookup/direct WHO return respectively; reuse
does not refer to the rejected C13 memo. The private default-on option reproduces
this rejected experiment only, not a production/default C10 change.

Audit removes exactly 137584 identity finds, keeping all Graph operations and
allocations unchanged. 816 independent fixture/scheduling runs pass, including
namespace shadowing, namespace reopening and extern completion. Same-binary
paired PUBLISH gain is -3.72 ms, CI -28.76–13.06, 7/15 wins. No speedup established.
Full result: `docs/PLS10_C15_ENDPOINT_STRUCTURAL_AUDIT.md`.
