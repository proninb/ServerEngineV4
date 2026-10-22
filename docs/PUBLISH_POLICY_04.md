# PUBLISH: content snapshots without BUILD tokens or durable flush

This aligns the implementation with the PROJECT.md invariant that BUILD/REBUILD
own durability. PUBLISH creates no reusable BUILD lineage, so it needs neither
USN/change tokens nor an explicit durable artifact flush.

PUBLISH configures File Context before configuration composition. Acquisition
jobs carry `capture_change_token=false`, suppressing both unchanged-token proofs
and token capture for configuration, source and discovered Header inputs.
Stable read locking, before/after metadata checks, complete byte counts,
timestamp observation and SHA-256 remain. Ordinary acquisition and streaming
proof APIs continue to capture change tokens by default for BUILD/REBUILD.

Full persistence now receives an explicit durability policy. PUBLISH skips both
the overlapping compiled-image flush and the final compiled/runtime flush.
REBUILD retains durable persistence, including compiled-flush overlap for large
images. BUILD's persistence paths are unchanged.

Successful PUBLISH means complete artifacts are visible to subsequent LOAD and
the resident Project has been published. OS writeback may continue after return;
PUBLISH does not guarantee artifact survival after a crash or power loss.

## Measurement

MSVC x64 Release + IPO, outside the filesystem sandbox, same dedicated full
UnitProXL path for both executables. The saved baseline includes the previous
three optimization steps. One warmup pair plus seven alternating measured pairs
of ordinary PUBLISH:

| Metric | Baseline median | Candidate median |
| --- | ---: | ---: |
| Total PUBLISH | 1474.450 ms | 1437.570 ms |

Paired median saving: **47.580 ms**, faster in **6/7 pairs**. This measures the
combined policy change, not an isolated speedup attributable to USN removal.
Sub-second PUBLISH remains unachieved.

A separate three-pair stage series measured persistence at
251.993 -> 225.665 ms. Candidate stage profiles report:

```text
acquisition_change_tokens=0
persistence_flush_enabled=0
compiled_flush_ms=0
compiled_flush_overlap_ms=0
```

All 24 normal/profile artifact sets, including warmups, matched both hashes and
passed independent saved-baseline audits outside the lifecycle timer:

- compiled.bin: `A49506C8DF1A999D68624F3A3FD0C317516015B7375DEB992CED99736B75522D`
- runtime.bin: `A0281B3BB48103A7CD339CCCD072D24F3524DB9932BC67B58A714906996B5EE6`

## Validation

The server and all test targets built in Release; all 22 CTest suites passed.
New regressions compare content-only snapshots with ordinary verified snapshots:
bytes, hashes and observations agree, while tokens remain absent. File Context
tests verify propagation of the PUBLISH policy into acquisition jobs and retained
physical state. Existing proof tests continue to exercise default token capture.

All nine small integration cases passed byte parity/audits or matching failure
diagnostics. Full UnitProXL REBUILD and subsequent no-change BUILD passed against
both executables with matching compiled/runtime artifacts and independent audits.
Final PUBLISH passed an audit and removed BUILD acceleration state.

Raw runners, per-run outputs, measurements and audit/build/test logs are under
`build/publish-policy-04/`.
