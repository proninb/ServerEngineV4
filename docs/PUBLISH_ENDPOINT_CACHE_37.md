# Whole Source endpoint cache: rejected (37)

Retested complete typed suffix caching on the current ready-token / batched
string implementation. The existing member-transition cache already removes
most member searches. Exact immutable lexical suffix + root type keys reused
resolved steps, final type and dependency events at their original token
positions. Version 2 also reused the already-canonical Graph endpoint_ref,
with slot generation checks for queued operations and cache collisions.

| Separate native eight-block comparison | Baseline | Candidate | Saving, 95% CI |
| --- | ---: | ---: | --- |
| V1 resolved paths | 1379.772 ms | 1414.514 ms | -34.742 [-52.742, -16.742] ms |
| V2 paths + canonical endpoint_ref | 1403.117 ms | 1434.686 ms | -31.569 [-44.705, -18.434] ms |

Both candidates lost all eight paired blocks. Each series excluded one warmup
block and included sixteen ordinary PUBLISH calls per variant in mirrored
ABBA/BAAB order. Every call checked golden SHA hashes and an independent
baseline audit. No builds or tests ran during timing. Both versions passed
frontend tests, nine integration scenarios and sixteen actual parser-error
comparisons. The targeted test verified more than 250 actual cache hits.

Stage counters: 441,419 hits and 388,670 misses. Member searches barely changed
(115,180 to 113,774). Lookup, hashing, copying and miss admission outweighed
the removed work. Similar caches had already regressed in 13–15; this retest
does not reverse that conclusion. Do not repeat the same cache design without
new evidence of a materially different cost structure.

The experimental feature, lookahead API, counters and test assertion were
removed from production sources. Earlier optimizations are retained.
Reproducible scripts, immutable executables, data and candidate source snapshots
are under build/publish-endpoint-cache-37/; the first series is in v1/.
