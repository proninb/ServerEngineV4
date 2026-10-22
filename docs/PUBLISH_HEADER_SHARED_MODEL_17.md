# Share the experimental physical Header model across roots

The existing optional Header effective-field model was built inside the
per-root loop. Its build scans the entire prepared physical closure, independent
of the active root or macro state. On the full fixture, that repeated the same
work for 2060 Header roots and made experimental full PUBLISH take roughly
7.3–7.8 seconds. The old aggregate model-build timer was not captured separately;
that range is complete PUBLISH time, not an isolated model measurement.

The physical model is now built once before the loop and shared read-only by
the parsers. Workers still do not intern names, expand macros or mutate Graph.
The active streaming parser retains macro guards, type resolution, canonical
Graph writes and root-local provenance. The unused raw Header model is also
skipped when the effective-field model supersedes it.

A second defect was exposed by existing frontend regressions: the direct field
path resolved a named type but omitted `record_dependency(identity)`. It now
records that dependency at the same point as ordinary type parsing, including
the semantic-reuse event and current root's Source Map entry.

A regression asserts that a three-root shared Header closure reports three
physical records, rather than rescanning the closure three times. Existing
root-local dependency assertions verify the provenance correction. The separate
new Source endpoint regression is retained after rejecting Source caches.

## Comparison with the ordinary parser

Both experimental CMake options remain OFF by default:
`SERVER_ENGINE_HEADER_MODEL_V2` and `SERVER_ENGINE_HEADER_MODEL_EFFECTIVE_V2`.
The ordinary parser never builds this physical model, so removing repeated
model construction does not accelerate that ordinary path.

Seven alternating paired uninstrumented PUBLISH runs, excluding warmups,
averaged 1405.67 ms ordinary versus 1441.97 ms shared-model candidate. Three
additional instrumented pairs averaged:

| Stage | Ordinary, ms | Shared model, ms |
|---|---:|---:|
| Complete PUBLISH | 1391.01 | 1445.25 |
| Header | 227.92 | 270.04 |

One instrumented candidate run built the shared physical model in 4.962 ms,
finding 24027 records and 334424 eligible fields. Those fields were committed
through the experimental path. This preparation is cheap, but model lookups,
name checks and the unchanged semantic stream still cost more than they save.
The feature is therefore not promoted to the normal PUBLISH path.

Measurements used Release + IPO, without PGO, native execution outside the
filesystem sandbox and without concurrent builds/tests. The shared-model build
passed frontend regressions, nine integration scenarios, exact compiled/runtime
artifact comparisons and independent baseline audits for every completed
benchmark run. Earlier measurements of the uncorrected model were stopped after
the provenance failure and are not used as a performance acceptance result.

The changes fix the existing experimental implementation; they do not establish
a production speedup or subsecond PUBLISH. The best previously controlled PGO
benchmark result remains about 1.335 seconds (experiment 12).

Final verification: all 22 CTest tests passed in both the ordinary and
experimental builds. A fresh ordinary PUBLISH produced the reference SHA-256
hashes for both artifacts, passed the independent baseline audit and left only
`compiled.bin` and `runtime.bin` in its output directory. Its single 1684 ms run
is a correctness check, not a replacement for the paired performance series.

Raw logs and binaries are under `build/publish-header-effective-16/` and
`build/publish-header-shared-17/`. The latter contains the fixed shared-model
comparison and a preserved `candidate.exe`.
