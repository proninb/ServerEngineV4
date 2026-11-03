# Header V2: UnitProXL cursor-detail

Commit: 26a7a2c. Release /O2, IPO OFF. Three measured runs after one warmup.
Input: build/unitproxl-benchmark-20261015-233545/project.json.
No production code, Graph schema, persistence format, or once semantics changed.

## Counts

- Header roots: 2060
- Record definition events: 730600; Graph types: 24027; ratio: 30.41x.
- Processed members: 19882845; Graph members: 727868; ratio: 27.32x.
- Parser advance calls: 107970437.
- Raw tokens: 109770630.
- Active include events: 395942.
- Identifiers seen/unchanged: 42948124/42948124; replaced/elided: 0/0.

## Timing medians

| Metric | ms | Kind |
| --- | ---: | --- |
| Header semantic parse + Graph | 4822.448 | wall time, instrumented |
| Constructors | 697.897 | inclusive timer |
| Graph define calls | 175.592 | timer |
| Parser advance | 4157.479 | sampled inclusive estimate |
| Cursor decode | 3015.222 | sampled inclusive estimate |
| Cursor advance | 3164.431 | sampled inclusive estimate |
| Seek next | 2860.032 | sampled inclusive estimate |
| Directive execution | 581.556 | sampled inclusive estimate |

These estimates overlap; do not add them. They cannot be substituted directly for the 4002 ms Header stage from the production PUBLISH profile.

The repeated frontend token work is the principal candidate. Graph define is a much smaller direct component; this does not exclude Graph lookup costs nested in other operations.
No macro substitutions occurred on this workload. This supports investigating a guarded fast path while preserving directive execution, scope, include dependencies, and provenance. It does not justify globally skipping headers.

Before semantic-result reuse, prove that macro state, semantic scope, dependency bindings, and provenance match. Validate candidate versus baseline with complete artifact/semantic checks, not just type/member counts.

Verification: build/header-v2-cursor-detail-verification.json records unchanged SHA-256 for compiled.bin and runtime.bin.
