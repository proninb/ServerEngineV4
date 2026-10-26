# PLS09-A — Alias Cost Breakdown

2026-10-24. **Diagnostic only; no algorithm adoption or production change.**
PUBLISH only. No workers, compact ABI, Runtime changes or BUILD/replay work.

## Method and correctness

Standalone `experiments/parallel-local-source-09` generates copies of PLS08-C's
Source hook, alias implementation and existing PLS03 observer. Algorithms,
containers, final-cell semantics, WHO/cell numbering and warning order are kept.
The current engine core is freshly built MSVC Release + IPO. Original engine and
PLS08 source files are untouched.

Three frozen executables run the same native full PUBLISH path on one private
copy of UnitProXL:

* BASELINE: unobserved PLS08 candidate, **not production V2**.
* PHASES: coarse mutually exclusive stage clocks only. No per-operation clocks,
  counters, request logging or allocator replacement.
* DETAILS: exact operation counters, C++ allocation requested payload and
  append-only canonical request descriptors. Per-method timers remain disabled.
  Reuse grouping/export happens after alias_ms stops, while bound paths exist.

The alias timer includes destruction of the temporary alias_linker and its
scratch indices. Endpoint release and binding catalog copy have separate coarse
boundaries. These additions observe existing work; they do not remove it.

Before timing, all three binaries independently passed 705,238 expected values,
367,144 bindings/native pointers, and the full 137,584-object storage/default
audit against the verified PLS08 image. All 2,116,048,327 object storage bytes have
the accepted SHA-256 `cb5aa2607c10f928b273ad8f5d33c29f3656190b3ec9597550cbf068a5f0d0e2`.
Every measured run also preserved exact compiled.bin/runtime.bin digests against
the unobserved **same-contract** baseline. SHA and deep checks are outside timers.

Three observer-specific CTest suites pass: 34 native resident positive oracle
invocations and 20 original negative Source invocations. Negative checks preserve
the expected error category/message; existing PLS08 origin/order fixtures remain
the semantic reference. This is not new arbitrary-C++ grammar coverage.

## Observer calibration

15 alternating BASELINE/PHASES pairs, then 5 BASELINE/DETAILS pairs. Intervals are
10,000-resample percentile bootstrap CIs for median paired observer-minus-base
differences. Each pair runs the complete native PUBLISH, persistence, resident
SHM and construction cleanup; resident teardown is reported separately as in C.

| Observer | Metric | Baseline median | Observer median | Median paired difference | 95% CI |
|---|---|---:|---:|---:|---:|
| PHASES, 15 pairs | Full operation | 2,843.85 ms | 2,772.12 ms | −12.30 ms | −52.95…+8.82 |
| PHASES, 15 pairs | Alias interval | 709.25 ms | 689.38 ms | −19.13 ms | −32.54…−0.88 |
| DETAILS, 5 pairs | Full operation | 2,832.36 ms | 3,263.97 ms | +432.66 ms | +382.16…+478.32 |
| DETAILS, 5 pairs | Alias interval | 708.54 ms | 924.47 ms | +220.07 ms | +202.89…+241.00 |

PHASES did not establish a full-operation overhead, but its alias interval shows
a small negative shift (~2.7%). Instrumentation/optimized code generation and
execution conditions are not perfectly neutral; this is **not an algorithmic
speedup** and is not proof of its cause. Coarse stages are useful for ranking
large cost groups, not an exact unbiased decomposition of the frozen original
executable. DETAILS materially perturbs both allocator layout and memory traffic:
**do not rank production CPU costs using its stage timings**.

Absolute times varied during the series. The earlier 781.78 ms PLS08-C median is
historical context, not a number reduced by this experiment. No optimization was
made. Do not pool these observer-calibration pairs with the old V2/candidate A/B.

## Coarse cost distribution

Stage ms below are medians from PHASES only. Percentage is the median of each
run's stage/alias ratio, rather than division of unrelated stage medians.
Stage medians/median percentages are not additive exact decompositions.

| Existing work | Median ms | Median per-run share |
|---|---:|---:|
| Reference resolution, sorted keys, final binding output | 208.95 | 30.21% |
| Write target canonicalization + dense heads | 118.01 | 17.57% |
| Re-canonicalize binding targets + duplicate heads | 112.16 | 16.28% |
| Build final binding catalog | 95.88 | 13.92% |
| Copy direct catalog into binding catalog | 31.10 | 4.80% |
| Binding-local scratch cleanup | 43.94 | 6.26% |
| Alias object/cache/index destruction | 34.31 | 5.15% |
| Binding warning/history processing | 16.84 | 2.54% |
| Actual value LWW/history processing | 9.51 | 1.45% |
| Final value ordering | 5.04 | 0.77% |
| Bound endpoint release | 0.008 | approximately 0% |

The reference-resolution row is inclusive of canonicalization, cache/visiting
work, cell interning, allocations and output formation inside that stage. It is
**not** measured pure reference-chain traversal time. Cleanup timings include
destructors at their actual execution boundary.

## Exact workload / allocation evidence

Five independent DETAILS repetitions have identical operation counts and path
reuse summaries. Counts do not assert equal per-call cost.

| Operation | Calls |
|---|---:|
| Canonical requests | 1,806,670 |
| Direct requests | 1,773,854 (98.18%) |
| Requests containing dereference | 32,816 |
| Flat index lookup/hash | 1,839,486 |
| Newly inserted canonical cells | 1,306,064 |
| Path steps hashed | 3,850,672 |
| Equality calls / compared steps | 533,449 / 989,552 |
| Dereference detection steps scanned | 4,586,124 |
| follow calls / misses / hits | 399,960 / 367,144 / 32,816 |
| Binding lookup | 367,144 |
| cache stores | 367,144 |
| visiting inserts / erases | 367,144 / 367,144 |

In this corpus the first final-reference pass has 367,144 follow misses and no
follow hits; write canonicalization later has 32,816 hits and no new misses.
There is no evidence here that long recursive chains dominate this corpus.
Cycle/forward/other negative fixtures still require correct guards; absence of
such cost on UnitProXL does not authorize removing those semantics.

Canonical request grouping `(WHO, typed path, follow-final mode)` finds
1,345,416 distinct keys, 421,340 repeated groups and at most 160 calls/key.
461,254 calls (25.53%) repeat an earlier key. This does not prove all repeats are
removable: final binding context and the lifetime of cached cell references must
remain valid. Most groups are not one globally hot object.
All 32,816 dereference-bearing keys are distinct in this grouping, matching the
32,816 dereference-bearing calls. Measured repetition is concentrated in direct
keys; this corpus does not demonstrate repeated expensive dereference paths as
the main opportunity for another path cache.

Alias-owned C++ requested allocations:

| Allocation owner stage | Calls | Requested payload |
|---|---:|---:|
| Build binding catalog | 367,209 | 77.76 MiB |
| Copy binding catalog | 367,145 | 16.40 MiB |
| Resolve references/cache/visiting/output | 734,368 | 96.84 MiB |
| Rebuild duplicate binding-target heads | 367,159 | 25.80 MiB |
| Write targets/canonicalization/dense heads | 98,457 | 121.85 MiB |
| Other alias work | 47 | remainder |
| **Total** | **1,934,385** | **362.68 MiB** |

The first four binding/reference stages account for **94.91% of allocation
calls**. Source inspection matches the counted work: `bindings = direct` copies
the map, reference evaluation inserts then erases visiting nodes and retains
cache nodes, and the diagnostics pass builds another binding-target heads map.
The 367,144 cache stores plus 367,144 visiting inserts account for 734,288
node entries. The complete reference stage has 734,368 allocation calls: another
80 calls include bucket/array/output growth. The stage total is not exactly two
allocations per binding; counters establish the dominant source without claiming
every machine instruction was sampled.

Peak tracked alias-owned live requested payload is 158.35 MiB; flat index capacity
is 64 MiB. These are neither process RSS nor all bound/result storage. Buffers
allocated before alias begin are excluded even if freed in alias evaluation;
result buffers may remain live after reporting. In raw JSON `frees` are attributed
to the allocation-owner stage, **not the stage executing the destructor**. Use
coarse cleanup clocks for destruction cost, not that counter as a free-site map.

## Conclusion and boundary for the next experiment

Dense Write Heads removed the proven write-head allocation problem; it did not
change binding catalogs, reference cache/visiting, the second binding-target
canonicalization pass or their destruction. Actual LWW is now a small part of
alias work. Endpoint release is not the missing large cost either.

The next narrowly scoped hypothesis should target binding/reference temporary
state (for example indexed cache/visiting over existing canonical cell IDs),
retaining cycle detection, final-context visibility, warning provenance and
determinism. That is only a candidate, **not implemented here**. The data do not
justify another identity database, changing final-cell semantics or a promised
millisecond saving. Any adoption still needs the frozen full-PUBLISH A/B and
independent semantic parity. Production V2 remains untouched.

Artifacts and reproduction: `experiments/parallel-local-source-09/README.md`;
`build/parallel-local-source-09/audit-01/summary.json`, per-run phase JSON, native
stdout/stderr, raw paired samples and all three frozen binaries with SHA-256.
