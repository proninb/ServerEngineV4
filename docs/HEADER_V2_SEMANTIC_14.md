# HEADER-V2-SEMANTIC-14 — Zero-Definition Macro Fastpath

## Decision

**ACCEPT** as a small, parity-preserving optimization of `semantic_preprocessor_v2::seek_next`, on published OPT-08 checkpoint `96b68aebd21fc4d801f99ac231fe47710509d9bf`.

## Production change

When `preprocessing.size() == 0`, a prepared identifier cannot be expanded, replaced or elided. After validating `raw->identifier` through `strings.contains()` (the same validation performed by `preprocessor::expand`), return the unchanged prepared token directly. Benchmark-only `identifiers_unchanged` profiling remains accurate. When definitions exist, the existing `preprocessing.expand` path is unchanged.

No grammar, source management, preprocessor directive semantics, Graph/WHO-WHERE, Source Map, persisted `compiled.bin`, runtime, SHM or ABI changes. In particular, `#define`, `#undef`, active conditional handling and predefines continue to use the original macro machinery when bindings exist.

This is a local `seek_next` fastpath, not a new macro cache.

## Correctness evidence (Windows UnitProXL)

- OLD/V2 Header semantic projection parity: **801,988 rows**.
- Exact cross-executable Graph + WHO-WHERE + Source Map parity: **229,557,898 bytes**; SHA-256 `06AE2653203F584DADCD112C7BA4E9ECA58EAF027E5013CD50AF22BCC781377F`.
- UnitProXL identifiers: **1,294,582**; active-definition encounters: **0**; skipped calls to `preprocessing.expand`: **1,294,582**.
- Macro fixtures: local directives, and project-configured predefines, both PASS. Local fixture: 16 identifier encounters, 10 with definitions, 6 bypasses, 2 replacements, 1 elision; predefine fixture: 9 encounters, 7 with definitions, 2 bypasses, 1 replacement, 1 elision. Exact canonical parity passed for both fixtures.
- New native regression checks no definitions → active definitions → `#undef` → no definitions, plus configured predefines and repeated `start()`.

## Performance evidence (Release, same binaries, wall ms saved = original − candidate)

| Independent series | Semantic saved (mean) | 95% CI | Total saved (mean) | 95% CI |
| --- | ---: | --- | ---: | --- |
| A/B, 31 pairs | +3.118 ms | −1.679 to +7.916 | +4.418 ms | −3.840 to +12.677 |
| Repeat A/B, 31 pairs | +5.213 ms | +0.406 to +10.020 | +7.265 ms | −0.078 to +14.608 |
| **Order-controlled ABBA/BAAB, 32 blocks** | **+1.526 ms** | **+0.356 to +2.696** | **+2.463 ms** | **+0.585 to +4.340** |

Order-controlled test: 16 ABBA and 16 BAAB blocks, 128 measured invocations; 2 warmup blocks excluded. Semantic saved: ABBA +0.936 ms, BAAB +2.115 ms; order-difference CI includes zero. Order control cancels a *linear* timing drift inside each four-invocation block, not every possible external disturbance. Profiled cursor times are nested estimates and were not used for speedup acceptance.

The accepted mean improvement is modest. It does not justify broader preprocessor changes or a new persistent cache.

## Git publication contract

Only three paths are included in this commit:

1. `server_engine/project/frontend/preprocessor_v2.cpp`
2. `tests/frontend_regression_tests.cpp`
3. `docs/HEADER_V2_SEMANTIC_14.md`

Do **not** merge benchmark-only `opt06-dump` changes or any OPT-09/10/12/STREAM experiments into the production branch. Publish only after Windows Release build, native regression CTest, UnitProXL OLD/V2 parity and remote branch SHA guard succeed. No forced push.
