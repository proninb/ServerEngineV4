# Offline control startup analysis 08

No replay or new process memory capture. Reads the nine saved CONTROL-07 CSV/JSONL
pairs, original stdout hashes and run manifest. The CSV schema is explicit in
first_divergence.py and mismatches fail. Each CSV value is checked against JSONL;
terminal allocation counters and boundary live/active signatures are checked.
Cumulative per-boundary allocation counters are unavailable, not inferred.

```powershell
python experiments/project-unload-startup-08/test_divergence.py
python experiments/project-unload-startup-08/first_divergence.py --root build/project-unload-control-stability-07/run-01 --spread-mib 1 --out build/project-unload-control-startup-08/first-divergence-NEW.json
```

Output must be a new file. First spread uses >= threshold inclusively. It reports
both Private Bytes spread and drift spread, with per-run values and all ranked
transition delta spreads. The 1 MiB threshold is diagnostic only.

Result and interpretation: docs/PROJECT_UNLOAD_CONTROL_STARTUP_08.md.
First threshold crossing is cycle 0 after the second UNLOAD; the interval includes
LOAD and native validation. No operation-level causal conclusion is made.
