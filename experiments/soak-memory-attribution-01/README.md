# SOAK-MEMORY-ATTRIBUTION-01

One new 600-second OFF control on the exact retained executable, no rebuild/training/PGO performance A/B. Prior USE files and release manifest remain immutable. Only --arm and executable/output selection were added to soak.py; original and executed harness plus the complete diff are retained in results/20261027.

```powershell
python experiments/tcp-pgo-release-02/soak.py build/tcp-pgo-release-02 --seconds 600 --arm ordinary
python experiments/soak-memory-attribution-01/analyze.py build/tcp-pgo-release-02 build/soak-memory-attribution-01
```

The OFF output directory must not already exist. Re-running needs a separately recorded output path, not overwriting the original failed control. Run with normal unsandboxed server access, unchanged fixtures and credentials already provisioned locally. No new phase snapshots or allocator interventions are in this control. Original gate: 10 warmups, >=600 measured seconds, last-five minus first-five per-case private median <=128 MiB and handles <=16 at UNLOADED/reconnect GET_STATE.

The OFF process completed normally; the harness returned 1 at the final memory assertion. OFF/USE both fail the unchanged gate. Analysis preserves full-duration and common-cycle comparisons; high-run spans and sample-held durations do not observe actual memory phases between checkpoints. Return fixture/reconnect labels are completed-cycle associations, not causal deallocation attribution.

Optional SVG plot uses ReportLab (bundled runtime supports it):

```powershell
python experiments/soak-memory-attribution-01/plot.py build/tcp-pgo-release-02 build/soak-memory-attribution-01
```

See docs/SOAK_MEMORY_ATTRIBUTION_01.md. The portable result contains the OFF JSONL/status, alignment CSV, analysis, harness copies/diff and SVG; prior USE JSONL is retained under experiments/tcp-pgo-release-02/results/20261027/soak-samples.jsonl and the original local release bundle. Binaries, PGD, corpus and credentials are not distributed here.
