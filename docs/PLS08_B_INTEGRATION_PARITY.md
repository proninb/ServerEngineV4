# PLS08-B — Integration Parity

2026-10-24. PUBLISH-only experimental integration, Windows x64 / pack 8,
MSVC Release + IPO. Production sources and BUILD/REBUILD/LOAD are unchanged.

## Scope and result

The supported Source projection now passes independent fixture expectations
through both the diagnostic image backend and the actual native PUBLISH
lifecycle: compiled.bin, runtime.bin, fixed resident SHM, query, and teardown.
This does not promote the B1 `projection_only` reader to unrestricted Header/
Source grammar support. No new Graph ABI, identity database, worker pool or
compact representation was introduced.

The fixture suite has 139 invocations:

| Suite | Invocations | Independent evidence |
|---|---:|---|
| Existing semantics | 46 | Positive/negative semantics, original origins, DFS order |
| Writer/query/SHM | 68 | 17 cases × forward/reverse extraction × forward/reverse physical writer |
| Explicit boundaries | 8 | Const cold construction, union overlap, Assign and scalar-reference write rejection |
| Native resident PUBLISH | 17 | Actual persisted images and resident values/native pointers |

All four CTest suites passed. The added cases cover multidimensional arrays,
record arrays, references to array elements/base members, inherited fields,
nested default references, enum/float/bool/const defaults, declaration
initializers, strings, three-root losing writers, empty Source and rebindings.
Python fixtures specify expected values and target-address relationships rather
than deriving the answer from the reducer under test. Native resident checks run
after the operation timer and are disabled in timed PLS08-C repetitions.

## SourceMap and real corpus

The cold probe now round-trips every logical Header/Source root and every inverse
physical-file index. It checks contribution data, contributor file, path via the
native/UTF-8 path boundary, file kind and empty roots. This supplements the
independent final producer/losing-writer checks; it does not replace them with
counts alone.

Fresh UnitProXL forward and reverse runs both passed:

| Check | Each run |
|---|---:|
| Objects | 137,584 |
| Final initializations / checked values | 705,238 |
| Links / checked native pointers | 367,144 |
| Root entries round-tripped | 5,434 |
| Physical-file entries round-tripped | 5,434 |
| Final/losing provenance occurrences checked | 1,209,966 |
| Semantic fingerprint | 13649286433448276567 |

Reports: `build/parallel-local-source-08/unitproxl-b/summary.json` and
`build/parallel-local-source-08-b-tests.log`.

PLS08-C additionally compares all final values and bindings against this
independently verified image in actual resident SHM. Its untimed object-storage
audit compares complete object bytes against native V2 on the compatible
UnitProXL corpus, including untouched Header defaults. Changed alias/LWW fixtures
use their explicitly specified new-contract result rather than old V2 bytes.

## Explicit supported-domain boundaries

* Different canonical paths overlapping union bytes remain a semantic reduction
  gap. The diagnostic backend rejects actual overlapping final storage ranges
  before Runtime execution or writing a verified image. No physical-order LWW.
* The timed native backend conservatively excludes Source paths through a union,
  including nonoverlapping uses. This cheap structural gate is narrower than
  the diagnostic backend's supported domain.
* Assign inputs are rejected; user Assign materialization is not implemented.
* A scalar literal assigned directly to a reference (`r.ref = 8`) is currently
  rejected by extraction as a binding without an endpoint. Explicit writes to
  the referent (`x = 8`, with `r.ref -> x`) are supported. This is an extraction
  coverage limit, not evidence of native reference storage failure.
* Const declaration initializers use matching native cold object construction;
  no qualifier stripping or mutable-init workaround is accepted.

Negative tests check rejection and absence of a newly written verified
experimental compiled artifact. They remove old test artifacts first so stale
success cannot hide failure. No claim of arbitrary C++ or all Header V2 grammar
coverage follows from the corpus/fixture results.

## Reproduce

```powershell
cmake --build build/parallel-local-source-08 --config Release --parallel 8
ctest --test-dir build/parallel-local-source-08 -C Release --output-on-failure
python experiments/parallel-local-source-08/run_real.py build/parallel-local-source-08/unitproxl-b-new
```

See `PLS08_C_FULL_PUBLISH_AB.md` for the separate full lifecycle experiment.
