# Header/Source V2 templates — 2026-11-12

Subsequent contract correction: using declarations now create distinct nominal
scalars. Statements below about folding using aliases describe the earlier
implementation. See [NOMINAL_USING_TYPES_20261112.md](NOMINAL_USING_TYPES_20261112.md).

Local implementation and integration checks. The full TEMPLATE-01–05 plan is
**not complete**: legacy BUILD/REBUILD remain unsupported. No commit, push,
deployment, PGO training or release adoption was performed.

## Supported grammar and instantiation

Primary `struct`/`class` templates accept named `typename`/`class` type parameters
and named `int` value parameters. The name IecArray is not special-cased.

```cpp
using PLC_INT = short;
using PLC_WORD = unsigned short;

template<typename T, int N, int Lower>
struct IecArray { T values[N]; };

struct Device {
    IecArray<short, 6, 1> Flags;
    IecArray<PLC_INT, 6, 1> Backup;
    IecArray<short, 6, 0> Other;
};
```

Definitions retain prepared grammar tokens during construction; they do not
create a concrete Graph type until instantiated. A construction-local registry
shares definitions across Header roots and the Source pass. Concrete fields,
constructor defaults and derived array types are published directly into the
existing G using the ordinary record parser. No second semantic graph or new
binary-format version is introduced. The registry is destroyed after parsing.

Type arguments include supported intrinsic, named, qualified and derived types,
including nested specializations. Adjacent `>>` closes nested arguments. Injected
class names and supported constructors work inside the instantiated record.
Existing limitations of record/method parsing still apply.

Canonical identities use semantic type spelling and every value argument,
including arguments unused in fields. Flags and Backup above have the same
type_ref; Other has a different type_ref, although its physical layout matches.
Alias spellings normalize to the same intrinsic type. Type keys do not embed
Graph allocation-slot numbers. Repeated identical instantiations reuse the
existing type. Conflicting definitions across Header roots are rejected.

`Lower` is a template parameter, not an implicit change to C++ array indexing.
For example `T values[Hi-Lo+1]` computes storage length but indexing still starts
at zero. No IEC lower-bound metadata or implicit index rebasing is added.

Source V2 can both use Header records containing specializations and instantiate
visible Header templates directly, for example `Box<short> direct;`. It captures
only the requested type tokens and invokes the same instantiation mechanism;
there is no extra scan of all Source input. New concrete types are complete
before persistence/layout generation.

## Constant-expression subset

Array bounds, int template arguments and integer member defaults support unary
signs, parentheses and binary `+ - * / %` with precedence. Evaluation uses checked
signed 64-bit arithmetic. Template int arguments must fit int; array lengths must
be positive. Division by zero and supported arithmetic overflow are errors.

This is not the full C++ constant-expression/type-promotion system. Arbitrary
constexpr names/functions, sizeof, casts, shifts, unsigned arithmetic rules and
general expressions are not implemented. In particular the evaluator does not
model every C++ intermediate integer type/promotion; TEMPLATE-04 remains partial.

## Validation

- 36 Header template fixtures plus semantic assertions: requested example,
  aliases, nested `>>`, named/pointer arguments, constructors, self pointers,
  strings, array shape, unused definitions, negative arguments, arithmetic,
  duplicate/conflicting names, parameter shadowing and invalid arguments.
- Existing frontend_regressions and compiled_project suites passed serially.
- Real TCP PUBLISH, RUN/FREEZE, UNLOAD, LOAD, repeated PUBLISH and fresh-process
  LOAD passed. Queried array referents, nested records, computed member defaults
  and direct Source specializations retained their values. Repeated PUBLISH
  produced identical compiled.bin/runtime.bin hashes for the same fixture.
- Six TCP rejection scenarios cover wrong/missing/extra arguments, zero bounds,
  division by zero and links between distinct specializations.
- An isolated full UnitProXL copy passed PUBLISH/LOAD artifact and native SHM
  parity. Native payload was 2,146,276,728 bytes; its SHA-256 matched across LOAD.
  This is correctness validation, not a performance A/B or release memory gate.

Evidence and reproducible harnesses:

- `tests/header_template_regressions.inc`
- `build/templates-final-tests-02.log`
- `experiments/header-templates-01/tcp_e2e.py`
- `build/header-templates-final-02/summary.json` (final executable + UnitProXL)
- `build/header-templates-final-confirm/summary.json`
- `experiments/header-templates-01/legacy_probe.py`
- `build/header-templates-legacy-probe-02/summary.json`

During an overlapping large-project run, one frontend test reported
`materialize frontend root`; the subsequent standalone suites passed. The failed
raw log is retained in build/templates-final-tests.log. No root cause is claimed.
The first TCP negative harness reused its successful fixture and discovered that
failed full construction removes prior artifacts; subsequent tests isolate each
negative project. No user project or frozen reference was used for these probes.

## Remaining stages and limits

| Stage | Current status |
|---|---|
| TEMPLATE-01 | Primary record definitions/parameters implemented; broader template grammar remains unsupported |
| TEMPLATE-02 | Concrete instantiation into G implemented for Header and Source V2 |
| TEMPLATE-03 | Canonical reuse and PUBLISH determinism verified; BUILD/REBUILD OPEN |
| TEMPLATE-04 | Checked arithmetic subset implemented; full C++ constexpr semantics OPEN |
| TEMPLATE-05 | V2/Graph/persistence/UnitProXL checks PASS; legacy integration and performance acceptance OPEN |

Defaults, parameter packs, template-template parameters, deduction, explicit or
partial specialization, template inheritance syntax and alias templates are not
implemented. `using` currently supports intrinsic scalar aliases. Definition
capture checks parameter grammar and balanced record tokens; record semantics
are checked on instantiation, not exhaustively for unused templates.

The existing legacy BUILD/REBUILD frontend rejects the new grammar. The isolated
probe rejected even unchanged BUILD and REBUILD. Its failed construction also
removed its fixture's persisted artifacts, consistent with the existing cleanup
path. Do not treat PUBLISH/LOAD success as BUILD/REBUILD support. Completing that
stage requires integrating template definitions and invalidation with the sparse
Graph delta path, rather than silently substituting a full PUBLISH for BUILD.

Previously prepared Task runtime and Header literal/static fixes were preserved.
