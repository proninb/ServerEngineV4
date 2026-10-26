# PLS10-C10 — typed-endpoint scratch lifetime

2026-10-25. **Successful experimental candidate; production V2 unchanged.**
One change only: retain vector capacity of the ordered committer's temporary
`source_typed_endpoint` across endpoint requests. No new semantic cache, Graph
API/index/ABI, Source model, queue, batching policy or dispatch change.

## Attribution before implementation

C9's 1795875 allocations inside apply_file were split using allocation-context
guards, not per-operation timers. The unchanged baseline audit is frozen in
`build/parallel-local-source-10-c10/subgroup-baseline/` with binary hash.

| Exclusive context | Allocations | Requested bytes |
|---|---:|---:|
| endpoint_request | 1622654 | 44749843 |
| String expansion outside commits | 31816 | 1282776 |
| Commit, Graph and provenance | 141307 | 339169513 |
| Remaining apply_file | 98 | 55550156 |

The endpoint context accounts for about 90.4% of allocation calls, even though
it requests much less payload than Graph/provenance. Context includes any
name/type work called inside endpoint_request; it is not a stack-sampled CPU
attribution. C9's conclusion about expensive apply_file is preserved.

## Implementation and lifetime contract

The previous endpoint_request constructed a new endpoint and vector each time.
The candidate uses one private `endpoint_scratch` member of ordered_committer.
The existing `begin_endpoint()` invokes reset(), clearing type, object_type and
path length before each request. Existing save() copies the completed path into
out.paths synchronously. No stored endpoint or callback retains a view into the
scratch buffer. Requests are nonrecursive and only the ordered commit thread
accesses scratch. LHS is saved before RHS extraction resets scratch.

All member/type checks, name lookups, temporal reference resolution, union
fallback and diagnostic decisions execute as before. Only vector capacity is
retained. There is no assumed maximum path length or fixed-size replacement.
Errors still abort at the same operation. The member dies with its committer.

Private experiment toggle `PLS10_C10_REUSE=0/1` selects the original per-request
temporary or retained scratch in the same executable. Reuse defaults to on only
in C10. C7's source and historical executable remain intact.

## Frozen full PUBLISH A/B

Results: `build/parallel-local-source-10-c10/ab-01/summary.json`, `samples.json`,
and per-run logs. MSVC Release + IPO, same UnitProXL path, four extraction
workers/window eight, 15 rotated/reversed four-arm blocks. Original C7, same
binary off/on, and production V2 are compared in one series. Native executable
has no C9 clocks, allocation observer, generated Graph/identity copies, or hot
C7 audit counters. No builds, audit or tests ran concurrently with timing.

| Arm | Median PUBLISH |
|---|---:|
| Production V2 | 1381.95 ms |
| Original C7 | 1528.74 ms |
| C10 off | 1517.72 ms |
| C10 reuse | 1421.33 ms |

| Paired comparison | Gain (positive = faster candidate) | Bootstrap 95% CI | Wins |
|---|---:|---:|---:|
| C10 off → reuse | 93.53 ms | 69.97–126.87 ms | 14/15 |
| Original C7 → reuse | **98.77 ms** | **81.53–141.56 ms** | **14/15** |
| Original C7 → C10 off | 7.29 ms | -1.29–34.45 ms | 10/15 |
| Production V2 → reuse | -33.51 ms | -77.26–-1.57 ms | 3/15 |

Same-binary off/on establishes a benefit independent of a proven code-layout
benefit; original-C7/off CI includes zero. Absolute times must not be compared
against C8/C9 series. No samples were discarded and hashing ran after child
PUBLISH/teardown.

The native ordered linking/commit timer (includes waiting) improves by 92.71 ms
paired versus C7, CI 77.70–107.41 ms, 15/15 wins. This is consistent with the
location changed, not a complete per-function CPU attribution.

Full-with-teardown median is 1847.68 → 1733.85 ms versus C7; paired gain 122.43 ms,
CI 60.72–183.23, 14/15 wins. Teardown noise is therefore reported separately.
The main adoption comparison remains the declared PUBLISH timer.

## Allocation and memory validation

Separate audit, same binary with toggle off/on:
`ab-01/c10-audit-0.json`, `c10-audit-1.json`. These are cold count runs and their
timings are not performance evidence. Counts cover C++ new/delete in Source
scopes on commit and extraction threads, excluding acquisition/Header allocations,
malloc/HeapAlloc and mappings. Requested bytes represent traffic, not RSS.

| Counter | Off | Reuse |
|---|---:|---:|
| Total Source allocation calls | 2597813 | 993193 |
| apply_file allocation calls | 1795875 | 191255 |
| endpoint_request allocation calls | 1622654 | 18034 |
| Total requested bytes | 1051877480 | 1010237464 |
| Peak live requested payload | 190142364 | 190142428 |

**1604620 calls removed** (-61.77% total), **41640016 fewer requested bytes**
(39.71 MiB cumulative traffic). Other allocation contexts are unchanged in
call counts and requested bytes. The peak grows by only 64 bytes, consistent
with retained scratch capacity; this is a time/traffic improvement, not a
peak-memory optimization. Process peak WS versus original C7 has paired median
gain zero, CI -1560576–1429504 bytes. No RSS benefit is claimed.

All 12 scoped identity/Graph operation counters are identical off/on:
1169037 identity finds, 137584 resolves/objects, 1216586 path interns,
705238 slot applies/initialization lookups, 367144 rebinds and adds,
767104 link lookups; scalar/batch initialization counts remain zero.
String batching and the extra path-intern work identified by C9 were not changed.

## Correctness

**816 fixture runs PASS**: 34 cases × 4 schedules × fast/off/audit × reuse off/on.
The suite verifies independent Runtime values/native pointers, temporal
reference behavior, negative diagnostics, exact warnings, const/enums, arrays,
inheritance, union rejection and view lifetime. New cases alternate deep,
shallow and empty/root paths, including reuse across Source files. Persisted
fixture bytes, error text and warning origins are also compared across reuse
off/on at the same paths (not just queried values).

Full UnitProXL cold checks pass for original C7, native off/on, audit off/on and
V2. Persisted compiled/runtime bytes match within their separate V2/C7 domains
after every timed run. Full native object storage is identical across all arms:
2116048327 bytes, SHA-256
`cb5aa2607c10f928b273ad8f5d33c29f3656190b3ec9597550cbf068a5f0d0e2`.
Golden hashing and cold scans are outside timed PUBLISH. No cross-contract
compiled-image equality with V2 is claimed.

## Decision

Keep reuse enabled in the C10 experimental candidate. The measured gain justifies
using it as the next experimental baseline. Production stays V2: C10 is still
33.51 ms slower paired in this series and does not meet the <1000 ms goal.
No production PUBLISH/BUILD/REBUILD/LOAD changes, persistent history or additional
WHO/endpoint index were introduced. Further work must target measured remaining
costs; do not infer a cache-locality improvement or propose more workers from
these allocation counts.

Reproduction: `experiments/parallel-local-source-10-c10/README.md`.
