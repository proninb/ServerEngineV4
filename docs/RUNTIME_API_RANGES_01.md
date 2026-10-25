# RUNTIME-API-RANGES-01 — not promoted

An experiment-only executor-local byte array remembers successful validation of
the seven immutable ranges in a type API. On first application, existing checks
run unchanged; subsequent applications reuse the successful result. Dynamic
type API index/base-offset checks, reference addresses, stores, children/repeats,
link-marker preservation and execution order remain unchanged.

This is validation once per used API **per executor**, not a global plan certificate.
No shared mutable state or new scheduler is introduced. Cache allocation failure
falls back to the old checks. The extra local allocation, cache lookup and branch
can offset savings from range checks; no fine-grained cost claim was measured here.

Compile-time macro `CW_RUNTIME_API_RANGES_AB` exists only in standalone targets.
No-cache remains the production implementation.

## Results

Same counter-free Release+IPO executable; no-SHA is common to both variants.
Two excluded warmup pairs + 20 alternating measured pairs:

- Full medians: 1313.66 ms reference / 1313.42 ms candidate.
- Paired gain median **-3.735 ms**, mean +1.775 ms.
- Bootstrap mean 95% CI **[-11.43, 13.7265] ms**.
- Candidate faster **9/20** pairs. No demonstrated full-PUBLISH gain.
- Peak WS medians practically unchanged (2573856768 / 2573813760 bytes).

All 44 full PUBLISH calls preserved both golden artifacts. Compiled/runtime suite
passed with both policies. No production adoption or extra bridge justified.
This result rejects the local memo implementation as an optimization; it does not
prove that a fully validated prepared program with a different execution boundary
cannot be faster.

Raw data: `build/publish-lifecycle-01/runtime-results-20261023/`.
Reproduce with `RuntimeRanges01`, selector `CW_RUNTIME_RANGES_REFERENCE=1/0`,
and the generic paired runner. The compiled suite wrapper is `LifecycleTests01`.
