# CONSTRUCTION-LIFETIME-01 — memory benefit, not promoted for speed

The experiment stores full_construction_context in an in-place optional. Reference
destroys it at the original end of scope. Candidate resets it after all persistence
workers join and before resident mappings/SHM construction. Resident publication
reads its own compiled/runtime images and caller-owned settings; no context data
is needed there. REBUILD keeps its original lifetime. Canonicalizer trace mode
disables early reset because its instrumentation owns context references.

No new heap owner, asynchronous destructor, background cleanup, reused pool or
history graph is introduced. Destruction is still inside full PUBLISH time.
The candidate is compiled only with `CW_CONSTRUCTION_EARLY_RELEASE_AB`; default
production lifetime remains unchanged.

## Results

Counter-free Release+IPO; no-SHA common to both variants; 2 warmup pairs + 20
alternating measured pairs. Golden compiled/runtime SHA-256 passed all 44 calls.

| Metric | Original lifetime | Early context release |
| --- | ---: | ---: |
| Full median | 1303.775 ms | 1310.000 ms |
| Peak WS median | 2574108672 B | 2231953408 B |

Peak WS reduction **326.30 MiB / 13.29%**. Paired full-time gain median
**-2.93 ms**, mean -6.611 ms, bootstrap mean 95% CI **[-17.176, 3.0615] ms**;
faster **8/20** pairs. No acceleration demonstrated; a small time penalty remains
possible. Retained resident format is unchanged.

Decision: retain the isolated candidate as a measured memory/time tradeoff, but do
not enable it as a production speed optimization. It can be revisited if reducing
peak memory becomes the priority. Do not add its memory benefit to the accepted
no-SHA production result, where lifetime remains unchanged.

The initial experiment counted early context destruction but left destruction of
other locals in unaccounted time (~2.30 ms median). Full PUBLISH total remained
correct and included all of it. Final telemetry sums early release plus remaining
scope cleanup. A separate four-call smoke check preserved both goldens and showed
unaccounted time 0.0101–0.0239 ms. It is an accounting check, not a second speed series.

Raw data: `build/publish-lifecycle-01/lifetime-results-20261023/`,
`lifetime-accounting-smoke/`. Frozen timing executable is in the results directory;
the current target includes the subsequent telemetry-only repair.
