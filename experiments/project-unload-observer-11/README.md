# Observer factors 11

Four shared-only modes, one saved original and one diagnostic executable.
See docs/PROJECT_UNLOAD_CONTROL_OBSERVER_FACTORS_11.md for results and limits.

prepare.py verifies exact injection anchors and creates a fresh copy of the actual
THREAD-06 source. observer11.hpp adds only five observer calls. Compile the copy
with /O2 /MD /EHsc /std:c++20 /Zi /Zo /DEBUG:FULL into the dedicated build folder.
Never overwrite the original replay.exe.

```powershell
python experiments/project-unload-observer-11/test_observer.py
python experiments/project-unload-observer-11/run_observer.py
python experiments/project-unload-observer-11/compare.py
```

Runner uses a new build/project-unload-observer-11/run-01, refuses overwrite,
and executes a balanced 16-run schedule. Common pipes/CWD/argv are explicit;
Popen executable/application name chooses original versus diagnostic while
argv[0] is fixed. Original CLI is unchanged. Four-character environment selector:
SE_OBSERVER11=off_ (original/off), mark, ack_. Other environment values are fixed.

Marker is stdout JSON observer11 label/PID; only ack blocks for stdin byte C.
No named events or address capture. Capture validates all markers and the existing
274-signature/counter contract before comparison. Environment values are not saved;
full and base digests are recorded. All normalized and raw outputs are preserved.
