# RESOLVED-G-01 — UnitProXL

Date: 2026-10-24. Diagnostic experiment; production PUBLISH remains unchanged.

## Result

| Measurement | Median |
|---|---:|
| Uninstrumented complete PUBLISH, 10 control samples | 1,313.645 ms |
| Uninstrumented Source stage, same controls | 424.027 ms |
| Resolved Source operations → G, 15 replays | **153.3827 ms** |
| Header-checkpoint clone/capacity preparation, excluded | 4.9729 ms |
| Final G/index verification, excluded | 6.5193 ms |
| Replay G release, excluded | 4.9718 ms |

Replay range: 145.7435–163.5072 ms. Two additional warmups are excluded. All 17 runs reproduce the original Source result, including index fields and deterministic handles; warmups additionally check every API output/replacement flag.

**This measures Source mutations into a G whose 24,027 Header types and 727,868 members already exist. It is not the total cost of building G from files.** The replay timer includes reading/dispatching the resolved tape and the existing resolved Graph APIs, including their allocation, validation and indexing.

153.38 ms is about 36.17% of the independently measured Source median. This ratio is an orientation, not an additive decomposition of production CPU. In particular the remaining difference cannot be attributed entirely to Parser, bindings or SourceMap. Cache/allocator conditions differ, and the replay has a compact, already-resolved input.

## Actual input distribution

| Resolved API category | Captured calls |
|---|---:|
| Object creation | 137,584 |
| Derived types | 524 |
| Path interning | 760,228 |
| Individual initializations | 196,469 |
| Batch initializations | 15,899 |
| Reference/link rebinding | 367,144 |
| Extern completion | 0 |
| **Total** | **1,477,848** |

Batch payloads plus individual calls contain 705,237 initialization records. Final G has 137,584 objects, 367,144 links, 705,237 initializations and 370,678 canonical paths. There are no replacement calls in this UnitProXL stream; repeated writes, repeated link targets and extern completion are exercised separately in the small correctness fixture.

The diagnostic tape retains 192,527,208 bytes of vector capacity (~183.61 MiB). The copied Header checkpoint's tracked main arrays occupy 55,805,860 bytes; final replay G arrays occupy 165,837,832 bytes. These are capacity accounting figures, not a process peak measurement. This tape must not be interpreted as a recommendation to add an intermediate production model.

## Observation control

Ten alternating control pairs, after two warmup pairs, compare a fresh standalone executable with capture disabled against the uninstrumented executable.

| Metric | Median paired difference (inactive − baseline) | Bootstrap 95% interval |
|---|---:|---:|
| Source | −0.163 ms | [−6.782, +9.1855] ms |
| Complete PUBLISH | −1.615 ms | [−28.980, +9.750] ms |

The disabled-hook cost is not statistically resolved in this series. Capture-mode PUBLISH/Source timers are intentionally excluded from production conclusions. No per-call stage timer is used in the Graph replay.

All 25 successful full UnitProXL PUBLISH calls in the final series (24 controls/warmups and one capture process) retain both golden digests:

- compiled.bin: `a49506c8df1a999d68624f3a3fd0c317516015b7375deb992ced99736b75522d`
- runtime.bin: `a0281b3bb48103a7cd339cccd072d24f3524db9932bc67b58a714906996b5ee6`

SHA-256 is checked by Python after each process and is outside the PUBLISH/replay timers.

The standalone CTest passes positive artifact/G parity and deliberate-corruption rejection. A first large-corpus equality failure came from comparing nonsemantic tail padding in a private link-index record; comparison now checks its exact key/handle fields instead. No Graph algorithm was changed to fix that diagnostic.

## Architectural conclusion

Physical resolved writes are substantial (~153 ms), but they do not explain the entire ~424 ms Source stage or the ~1.314 s PUBLISH. This result does not justify a new Source parser or another WHO cache. It supplies a reproducible Graph-only workload on which to investigate allocation/index costs before selecting one algorithmic experiment and validating it in full PUBLISH.

Implementation and commands: `experiments/resolved-g-01/README.md`.
Raw final series: `build/resolved-g-01/measurements-v2/{summary.json,controls.csv,replays.csv}` plus per-call stdout/stderr.
