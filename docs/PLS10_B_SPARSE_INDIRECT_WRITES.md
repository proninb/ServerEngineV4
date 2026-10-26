# PLS10-B — Sparse Indirect Writes

Date: 2026-10-25. Decision: **NO ADOPTION** of early direct-winner
preparation. Production V2 remains unchanged.

## Implementation and semantics

The isolated experiment keeps the A2 final-binding kernel and existing G ABI.
After existing type/value validation, a write-column sink retains current
direct winners and full pending records only for reference-dependent writes.
Losers retain only event/origin metadata. Finalization resolves pending paths,
selects max Source order per final cell and emits the exact ordered warning
transitions. Shared lazy interning preserves the previous canonical cell IDs.

This still retains other bound columns and the separate experimental Writer.
It is not PLS10-C, a new frontend or production integration. Extraction is
sequential in the measured harness. No BUILD/replay or new workers were added.

## Correctness

- 13/13 PLS10B CTest suites pass, including candidate reference-dependency tests.
- Seven additional merge cases run in both strategies and forward/reverse
  order (28 runs): interleaved direct/indirect writes, late rebinding,
  indirect winner, string/array ties, explicit externs and 1000 direct writes
  to one cell. Warnings preserve `1 -> 2 -> 3`, not only `1 -> 3`.
- UnitProXL semantic fingerprint: `13649286433448276567`.
- Canonical numbering fingerprint: `7605832117458648793`.
- Cold compiled-query, SourceMap, values and native pointers checks pass.
  Full 2,116,048,327-byte object storage SHA-256 matches the accepted oracle:
  `cb5aa2607c10f928b273ad8f5d33c29f3656190b3ec9597550cbf068a5f0d0e2`.
- Compiled/runtime digests match between A2 and B after every timed run.
  V2 artifacts are a separate semantic domain; no V2 byte parity is claimed.

## Frozen full-PUBLISH A/B

Artifacts: `build/parallel-local-source-10-b/ab-01/summary.json`,
`samples.json`, frozen executable hashes and cold allocation profiles.
Release + IPO, same executable/fixture per comparison, alternating arm order.
Cold oracle checks and file hashing are outside the reported PUBLISH timer.

| Metric | A2 control | B | Median paired effect |
| --- | ---: | ---: | ---: |
| PUBLISH, 31 pairs | 1766.88 ms | 1830.75 ms | **+62.90 ms regression** |
| Alias/reduction | 165.989 ms | 130.768 ms | 35.793 ms faster |
| Full with resident teardown | 2078.13 ms | 2153.77 ms | +71.72 ms regression |

95% paired bootstrap interval for PUBLISH regression: **56.12..67.89 ms**.
B wins only 2/31 PUBLISH pairs. Alias improves in 31/31, but this is shifted
work rather than a full-path improvement. Peak working set versus A2 is
inconclusive (paired interval includes zero).

Separate V2/B series, 15 pairs: 1334.75 / 1906.98 ms medians;
B regression 569.11 ms, 95% interval 529.59..624.76 ms, 0/15 wins.
These fresh series must not be compared by subtraction to historical timings.

Stage medians within the A2/B series:

| Stage | A2 | B |
| --- | ---: | ---: |
| Extraction | 155.202 ms | 152.935 ms |
| Typed linking / direct preparation | 307.563 ms | 397.320 ms |
| Alias/reduction | 165.989 ms | 130.768 ms |
| Writer | 248.986 ms | 259.914 ms |

Stage medians do not sum to the median total. The measured increase is in
the phase containing the new direct index; it does not separately identify
hashing, lookup, allocation or cache costs.

## Why early reduction did not help this corpus

705,238 writes comprise 672,422 direct writes and 32,816 pending indirect
writes (4.65%). There are **zero early direct losers**. Retained write rows
remain 705,238 before and after final reduction. Thus the early winner index
eliminates no write payload on UnitProXL, while requiring prekeys, mapping and
earlier index access. Tiny duplicate-heavy fixtures prove its semantics but
are not evidence of performance on this corpus.

Source-inclusive cold allocation audit (extraction + linking + finalization):

| Metric | A2 | B |
| --- | ---: | ---: |
| Allocation calls | 2,377,552 | 2,377,618 |
| Requested bytes | 1,546,311,586 | 1,585,487,094 |
| Peak live requested bytes | 353,733,594 | 356,553,357 |
| Final live requested bytes | 201,559,021 | 201,559,021 |

These are observed requested payloads, not process RSS. They include producer
work so moving allocations before alias stage cannot hide them. Instrumented
stage times are diagnostic only. The older A2 alias-only allocation count has
a different scope and must not be compared directly to these totals.

## Next boundary

Do not adopt the early direct-winner index. The next isolated variant should
apply direct writes immediately through existing G APIs, keeping only temporary
Source order/provenance needed for replacement. Preserve pending full records
only for indirect writes, then apply them through the same operation after
final bindings. Merge diagnostic event metadata in Source order so delayed
application still yields exact adjacent replacement warnings.

This requires checking G replacement and SourceMap contribution behavior;
the present experiment does not prove that boundary. It must not introduce
another full direct-operation array or extend the persistent G format.

Reproduction and selectors: `experiments/parallel-local-source-10-b/README.md`.
