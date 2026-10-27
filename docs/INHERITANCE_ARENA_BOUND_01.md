# INHERITANCE-ARENA-BOUND-01 — audit preparation

2026-10-25. Production cache algorithm is unchanged. No arena cap has been selected or enabled.

`CW_SOURCE_INHERITANCE_ARENA_AUDIT` enables counters and the resolver's `inheritance_arena_audit()` snapshot:

- cache requests and successful hits;
- overwritten occupied slots and retired base steps;
- current occupied slots and live base steps;
- appended base-step size and vector capacity.

For successful appends, `stored_base_steps - live_base_steps` measures unreachable ranges. A vector allocation failure can leave additional unreachable storage depending on the container's exception behavior; snapshots remain the authoritative size accounting. Capacity is monotonic for this resolver because the vector is never shrunk. Capture per-resolver snapshots before destruction; summing capacities across non-overlapping lifetimes is not process peak RSS.

The flag changes class layout. All translation units using this class must be rebuilt consistently; do not link an audit consumer to an old library containing the same class definition. No per-operation clocks are added. When the flag is absent, fields, increments and snapshot API are absent.

Validation completed: MSVC C++20 `/Zs` compilation of a translation unit including the resolver with the audit flag. Dynamic collision churn, corpus hit rate, allocations and RSS have not yet been measured. Native execution is paused while the Norton detection affecting the independent PGO experiment is unresolved.

Next: collision-churn fixture and a normal reuse control, then corpus audit. Use measurements to choose a bounded-cache fallback candidate separately; do not infer an appropriate cap from the 65,536 slot count alone.

## Synthetic cache audit — 2026-10-26

Implemented `experiments/inheritance-arena-bound-01/prepare.py` and `churn.cpp`. The generator renames the resolver and exposes its cache primitives in a generated header, avoiding linkage against a differently configured production class. The test calls the actual cache methods with synthetic valid handle bit patterns; it does not exercise Graph member lookup or claim corpus semantic parity.

MSVC C++20 `/O2` run passed:

| 100,000 accesses | Reuse one key | Alternate two colliding keys |
|---|---:|---:|
| Overwritten slots | 0 | 99,999 |
| Retired base steps | 0 | 99,999 |
| Live base steps | 1 | 1 |
| Stored base steps | 1 | 100,000 |
| Capacity in base steps | 1 | 138,255 |

The fixture verifies the returned member/type/owner/path and `stored - live == retired`. Counter requests include the explicit successful verification read immediately after each cache insertion, so their hit rate is not a workload hit-rate measurement. The first fixture run used an invalid synthetic type tag and failed its check; correcting the tag produced the passing result above.

This proves history-dependent arena growth with a fixed number of live slots. It does not measure process RSS, UnitProXL churn, allocations or PUBLISH performance. Production remains uncapped. Next required evidence is the corpus snapshot before choosing a byte limit and separately validating uncached fallback.
