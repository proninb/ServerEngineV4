# Type-plan investigation: rejected flat program index

Temporary coarse instrumentation of full PUBLISH attributed type-plan work to:

| Stage | One diagnostic run, ms |
|---|---:|
| Objects and default type programs | 61.473 |
| Canonical unconnected type programs | 21.302 |
| Relative-reference program canonicalization | 10.223 |
| Reference validation and root groups | 0.553 |

These are one-run diagnostics, not comparative performance estimates. The
instrumentation was removed before comparative benchmarks. Source is the
larger serial stage; type planning overlaps runtime link/initialization planning.
Only the maximum of the two physical-plan durations contributes to this part
of PUBLISH's critical path. Saving 10 ms in an 85 ms type plan cannot save 10 ms
when a concurrent runtime plan already needs around 75–80 ms.

A flat open-addressed temporary index was tried instead of an unordered map of
per-hash vectors when canonicalizing exact relative-reference programs. Hashes,
full equality checks, first-occurrence order and the final transformation proof
were preserved. The change passed compiled-project tests, nine integration
scenarios and independent full-project audits with matching artifact hashes.

Seven paired uninstrumented runs averaged 1426.65 ms baseline and 1425.28 ms
candidate: only about 1.37 ms. Three instrumented pairs showed physical-plan
means of 86.06 and 85.70 ms, while complete PUBLISH regressed by about 5 ms.
This did not justify adopting the index. The source was restored exactly;
no type-plan algorithm or format change remains from this experiment.

The raw scripts, logs and measurements are in `build/publish-type-plan-09/`.
