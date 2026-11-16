# Nominal scalar using declarations — 2026-11-12

Implemented locally for Header/Source V2. This supersedes the earlier statement
that using and typedef both fold to intrinsic types. No commit/push/deployment.

```cpp
using PLC_INT = short;
using PLC_WORD = unsigned short;
using PLC_REAL = float;
```

Each using declaration now defines a distinct named scalar in ServerEngineV4.
PLC_INT differs from short and from another named type backed by short. Repeating
the same declaration resolves the same identity. A declaration such as
`using NEXT_INT = PLC_INT;` creates another distinct type with short storage.
Qualified namespace identities remain distinct. This deliberately differs from
standard C++ alias semantics and applies to all supported scalar using declarations,
not just PLC-prefixed names. typedef retains its previous folding behavior.

Storage size/alignment, numeric initialization and native representation follow
the underlying scalar. The Windows x64 checks confirmed 2/2/4-byte storage for
PLC_INT/PLC_WORD/PLC_REAL. Out-of-range numeric storage and floating-to-integer
initializers are rejected. References and links require the exact nominal type;
sharing an underlying intrinsic is insufficient. Template specialization keys
preserve nominal identity: Box<PLC_INT> differs from Box<short>, while a legacy
typedef short alias still shares the Box<short> specialization.

## Representation and compatibility

G uses graph_type_kind::nominal_scalar, distinct from intrinsic_alias and enum.
ABI/layout/materialization read its storage intrinsic; semantic type_refs remain
named. Constructor defaults, Source writes and Runtime queries preserve this split.

Images containing nominal scalars use compiled.bin v28. Images without them keep
v25 (ordinary types) or v26 (enums). The reader accepts v25/v26/v28; the previously
experimental Direct-mmap v27 is not reintroduced. v28 prevents older executables
from interpreting the new type kind. Old files are not retroactively converted
from aliases to nominal types: their original persisted semantics remain intact.
Files must be republished from the new using declarations to obtain nominal types.

## Verification

- Full frontend_regressions and compiled_project suites PASS.
- Direct Graph/persistence regression confirms independent identities, v28
  round-trip and rejection when nominal metadata is disguised as v25/v26.
- Real TCP PUBLISH, RUN/FREEZE, UNLOAD, LOAD and fresh-process LOAD PASS, including
  exact scalar byte sizes, float bits, named type queries, constructor overrides,
  Source initializations and same-type references.
- Cross-links to short, OTHER_INT and NEXT_INT reject; overflow and incompatible
  floating initializers reject. Native SHM/artifact hashes match across LOAD.
- Updated generic template integration and isolated full UnitProXL PUBLISH/LOAD
  artifact/native-SHM parity PASS. No performance A/B or release soak is claimed.

Evidence:

- build/nominal-using-tests-final.log
- build/nominal-using-e2e-03/summary.json
- build/nominal-using-templates-unit/summary.json
- experiments/header-templates-01/nominal_using_e2e.py
- tests/header_template_regressions.inc
- tests/compiled_project_tests.cpp (test_nominal_scalar_persistence)

The first ABI/type-query harness revision used the wrong GET_TYPE payload key;
its failure remains in build/nominal-using-e2e-02. The corrected harness passed.

Legacy BUILD/REBUILD still lack using/template grammar; this change does not claim
to complete that previously open integration. Scalar using declarations are
supported; aliases to records, arrays, pointers and alias templates remain outside
the current grammar. PGO and heap policy settings were not changed.
