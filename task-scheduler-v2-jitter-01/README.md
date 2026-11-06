# TASK-SCHEDULER-V2-JITTER-01

Read-only analysis of existing TASK-RUNTIME-V2-PERFORMANCE-01 A/B stage CSVs. Does not edit C++ or run the server.

## Run (PowerShell, repository root)

```powershell
python .\task-scheduler-v2-jitter-01\analyze.py `
  --off .\build\task-scheduler-v2-01\off `
  --on .\build\task-scheduler-v2-01\on `
  --out .\build\task-scheduler-v2-jitter-01
if ($LASTEXITCODE -ne 0) { throw 'Jitter analysis failed' }
```

Produces `jitter-summary.csv`, including cycle and barrier p50/p95/p99, the fraction of cycle intervals above 2 ms and 4 ms, and a **proxy** based on interval and tick medians.

The proxy is not actual wake-up latency. Intervals above 4 ms are not necessarily skipped logical cycles. Exact deadline misses require timestamp instrumentation at scheduler wait and dispatch boundaries. The analyzer will fail with an explicit schema message if the stage CSV differs from the supported format; it never silently fabricates samples.

No changes to default mode, PGO, deployment, commit or push.
