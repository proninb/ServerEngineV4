# Shared control stability 07

Uses the saved THREAD-06 replay.exe, strict-replay.bin and stages.bin unchanged;
the locally implemented interface differs from the unprovided ZIP's TSV/CSV CLI.
Only single mode is executed. See docs/PROJECT_UNLOAD_CONTROL_STABILITY_07.md.

```powershell
python experiments/project-unload-control-07/test_control.py
python experiments/project-unload-control-07/control_stability.py --out build/project-unload-control-stability-07/run-NEW --runs 9
```

The output directory must not exist. Executable/input/stages/boundary SHA-256
checks occur before and after each process. Original JSONL and stderr are kept;
CSV is derived only after validation. Timeout terminates only the launched replay.
Seven tests cover completion, signature, counters, drift consistency, single-mode
stack rejection and phase variability. No production server/allocator is changed.

Phase uses pairwise cycle 70/80 and 90/80 signs plus selected peak/trough cycle.
The report separates stable signs from unstable peak amplitude/position.
Inputs and executable are fixed to existing build paths; no binary is rebuilt.
