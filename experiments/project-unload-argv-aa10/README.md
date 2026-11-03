# Unchanged argv A/A control 10

Uses original THREAD-06 replay.exe and binary input. No C++ build or observer.
See docs/PROJECT_UNLOAD_CONTROL_ARGV_AA_10.md for contract, results and limits.

```powershell
python experiments/project-unload-argv-aa10/test_argv.py
python experiments/project-unload-argv-aa10/run_argv.py
python experiments/project-unload-argv-aa10/compare.py
```

Run from repository CWD. Runner refuses to overwrite its fixed fresh output
directory build/project-unload-control-argv-aa-10/run-01. Schedule is ABBA x4,
A absolute input/stage arguments, B relative. argv[0], mode single, CWD and
one explicitly passed environment snapshot remain fixed. stdin is inherited,
matching the actual CONTROL-07 Popen call. Environment values are not saved.

Hashes are checked before/after each process and paths are resolved to the same
input. Existing CONTROL-07 274-signature and terminal-counter validation must
pass for all 16 runs before compare.py aggregates anything. Outputs stay JSONL;
derived CSVs and comparison.json are additional evidence, not substitutes.

Actual ADDRESS-09 protocol is stdout JSON marker + stdin byte C, not named events.
No observer-off runtime switch exists in that saved diagnostic executable; this
experiment does not create one or run observer comparisons.
