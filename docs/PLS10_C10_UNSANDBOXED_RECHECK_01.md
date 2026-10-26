# PLS10-C10 versus V2 — unsandboxed recheck

2026-10-26. **C10 is slower than historical production V2 outside sandbox.**

| Native PUBLISH median | ms |
|---|---:|
| Historical V2 | 1353.49 |
| C10 scratch reuse | 1426.55 |

Difference of medians: C10 +73.06 ms (+5.40%). Median of paired C10-minus-V2
differences: **+83.06 ms**, bootstrap 95% CI **38.86–103.71 ms**. V2 wins
**15/15** pairs. Do not confuse the two median statistics.

With resident teardown included: V2 median 1678.14 ms, C10 1738.22 ms. Paired
C10 penalty 74.45 ms, CI 23.55–103.61 ms. Peak WS medians: V2 2571636736 B,
C10 2565750784 B; this is process-lifetime memory, not Source-only memory.

## Method

Executed through `exec_command` with `sandbox_permissions=require_escalated`.
One runner outside sandbox launched all children; no binary rebuild or input
regeneration. Same original C10 fixture/project path, cwd, artifact directory
and cleaned inherited environment. Four candidate extraction workers, window
eight, C7 fast path enabled, C10 reuse enabled. V2 uses PLS08_CANDIDATE=0;
C10 uses 1. Trailing expected-type argument is 0 in both commands; the harness
checks actual counts: 24027 types / 137584 objects / 367144 links.

Two preflight object-storage audits, one warmup per arm, then 15 alternating
pairs. Every sample retained. Native internal total_ms ends before resident
teardown; hashing occurs after process completion. No builds/tests ran during
timing. Two additional object-storage audits follow timing. Six extra operations
are excluded from the 30 native observations. Bootstrap uses 20000 resamples of
paired differences with seed 110.

Frozen binaries match the original C10-series hashes:

- V2: `841c7848aaf4631fef25c8bf6eb28de7cac072f9b01dd66dcf0cae7b0a1adde5`
- C10: `bed3d84c433f2c78b1a1a33083aef41cccca38c4a52778097c2d2e11db8abe38`

Input and binary hashes are unchanged. Original fixture artifacts were backed
up and restored. All outputs match each arm's historical compiled/runtime gold.
V2 and C10 use different persisted representations, so their complete artifact
hashes are not asserted equal across arms. Common resident object-storage digest
before and after timing:
`cb5aa2607c10f928b273ad8f5d33c29f3656190b3ec9597550cbf068a5f0d0e2`.
This is an object-storage audit, not a complete historical live-SHM/G equality
proof. The existing broader C10 correctness report remains separate evidence.

Results: `build/pls10-c10-unsandboxed-01/` contains frozen binary copies, input
manifest, commands, every stdout/stderr, samples.json, summary.json, checks.json,
verified.json and original artifact backups. Runner:
`experiments/parallel-local-source-10-c10/recheck_unsandboxed.py`.

Run **outside sandbox**, with a new output directory:

```powershell
python experiments/parallel-local-source-10-c10/recheck_unsandboxed.py build/pls10-c10-unsandboxed-01-new
```

This comparison is C10 versus the frozen historical V2, not versus the latest
modern fast-reader executable. The old series' launch mode remains unknown;
this recheck explicitly establishes the unsandboxed result. Production code is
unchanged. No basis to replace V2 with C10 is demonstrated.
