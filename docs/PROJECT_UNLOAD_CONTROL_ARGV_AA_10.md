# PROJECT-UNLOAD-CONTROL-ARGV-AA-10

2026-10-29. **CAPTURE/VALIDATION COMPLETE / argv spelling alone insufficient /
cause OPEN / NO ADOPTION / release BLOCKED.**

16 independent runs of the original unchanged shared-only executable; 8 absolute
and 8 relative input/stage path arguments, schedule **ABBAABBAABBAABBA**.
No hooks, VirtualQueryEx, ETW, private domain, new C++ build, server launch,
observer-on/off run or production change.

The mentioned ZIP was not attached or found locally. A local runner uses the
actual verified CONTROL-07 command contract, not the previously proposed TSV CLI.

## Fixed launch contract

Executable SHA-256:
`e29c95e4d4e4bea399fa532683656f8a39f1e5e737a9b09428056feb35b1c77b`.
Input SHA-256:
`7571f8b57e92fefbf75954794d6d193b6c4e16c781d21528cc61988ed3e0ab8e`.
Executable, input, stages.bin and boundaries.json hashes match before and after
each process. All 16 PIDs are distinct and all exits are zero.

CWD: `C:\Boris\ServerEngineV4`. argv[0] is the same absolute executable path.
Mode is always `single`. Only argv[1]/argv[2] differ:

```
A: C:\Boris\ServerEngineV4\build\project-unload-thread-06\strict-replay.bin
   C:\Boris\ServerEngineV4\build\project-unload-thread-06\stages.bin
B: build\project-unload-thread-06\strict-replay.bin
   build\project-unload-thread-06\stages.bin
```

Each relative argument is resolved and checked to target exactly the corresponding
absolute input before launch. Actual CONTROL-07 uses Popen without a stdin
argument; this experiment explicitly preserves **stdin=None / inherit**, not
DEVNULL, a pipe, or named-event synchronization. Creation flags remain 0x08000000.
Stdout/stderr are separate per-process files, as in CONTROL-07.

One environment dictionary is copied once and explicitly passed to every child.
Its canonical digest is checked throughout:
`62c8c4e95ae1cbe8761c36b0d898df7acbb2a44a6c47230678fd5e50bace780d`.
Only digest and key names are saved; values are not published because inherited
environment may contain secrets. Fixed values within this run do not establish
environment equality to historical experiments or freeze OS/background activity.

## Validation before comparison

All 16 original JSONL streams pass the existing CONTROL-07 validator:
274 boundary ID/stage/active-worker/live-count signatures; baseline-relative
drift consistency; zero physical worker stacks in single; terminal Alloc 9536484,
matched Free 9536471, unmatched Free 159, live 13, logical starts/stops 8781/8781.
The comparator independently repeats validation and checks preserved stdout hashes
and launch/input/environment identity before aggregation. Derived CSVs retain
all boundary samples. Twelve contract tests PASS; Python syntax checks PASS.

## Results

Absolute PrivateUsage in MiB; each cell is **min / median / max** within eight
processes. Quiescent maxima are baseline-relative drift, also in MiB.

| Metric | A: absolute | B: relative |
|---|---|---|
| b01 | 153.539 / 153.947 / 155.086 | 153.293 / 154.689 / 155.133 |
| b02 | 149.480 / 149.670 / 150.355 | 149.309 / 149.625 / 150.848 |
| b03 | 149.051 / 149.400 / 151.023 | 149.094 / 149.412 / 149.855 |
| b03 spread | **1.972656** | **0.761719** |
| b02 → b03 delta | -0.887 / -0.430 / +1.352 | -1.676 / -0.098 / +0.188 |
| Quiescent maximum drift | **16.055 / 27.467 / 67.879** | **20.137 / 42.846 / 72.527** |

Both fixed-argv groups retain large within-arm variation: quiescent maximum
ranges span **51.824 MiB** and **52.391 MiB** respectively. b01 spread exceeds
1 MiB in both. b03 medians differ by only **0.011719 MiB**, while their spreads
differ. The diagnostic 1 MiB spread threshold does not replace the release gate.

| Selected cycle drift, MiB (min / median / max) | A | B |
|---|---|---|
| 70 | 7.141 / 14.547 / 65.758 | 3.770 / 7.602 / 16.125 |
| 80 | 3.379 / 3.639 / 12.621 | 3.355 / 12.963 / 72.527 |
| 90 | 3.336 / 3.910 / 67.879 | 3.246 / 13.455 / 67.613 |

No process reproduced >128 MiB drift. Eight runs per group and overlapping,
variable distributions do not establish statistical equivalence or prove the
absence of an argv effect. The different group medians are descriptive, not a
measured allocator mechanism or permission to adopt an allocation domain.

## Decision and verified observer protocol

Path spelling **does not fully explain** control variability: it persists within
each group with argv, environment, CWD, input and binary held fixed. An argv
contribution is not ruled out. Initial heap/layout, timing and other causes remain
OPEN. The original server's failing memory profile remains unreproduced.

Before any separately authorized observer-off/on experiment, the local ADDRESS-09
protocol has now been checked against its saved header and runner:

1. Child prints JSON `{"checkpoint09":"label","pid":PID}` to stdout and flushes.
2. Child blocks on Win32 ReadFile from stdin, requiring exactly one byte **C**.
3. Harness captures external maps, writes C and flushes the stdin pipe.
4. Labels are baseline, b00, b01, b02, b03. No Win32 named events are used.

The saved diagnostic executable has no observer-off runtime mode. A future
same-binary on/off experiment needs a separate diagnostic design; it cannot
silently reuse a named-event kit or treat the existing original and diagnostic
binaries as one executable. No such change or experiment was performed here.

Production C++, allocator, UNLOAD, TCP, original replay and PGO unchanged.
OFF/USE/final PGD hashes verified unchanged. PGO OFF. No commit, push or deployment.
Release BLOCKED; NO ADOPTION.

## Evidence

Result: `build/project-unload-control-argv-aa-10/run-01/comparison.json`.
capture.json retains each exact argv/CWD/PID, environment digest, before/after
hashes, exit code, validation result and terminal counters. Sixteen JSONL/CSV
outputs and stderr files are preserved, with the executed runner archived.
Sources: `experiments/project-unload-argv-aa10/`.
