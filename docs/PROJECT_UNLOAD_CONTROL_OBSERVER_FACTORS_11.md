# PROJECT-UNLOAD-CONTROL-OBSERVER-FACTORS-11

2026-10-29. **CAPTURE/VALIDATION COMPLETE / cause OPEN / NO ADOPTION /
release BLOCKED.** All 16 runs valid. No production or original replay change.

The referenced ZIP was not attached or found locally. The actual THREAD-06 source
was inspected, copied and given only a baseline hook and hooks at boundaries
0..3. Parent source hash matches the parent used in ADDRESS-09. The diagnostic
copy was built once with MSVC x64 /O2 /MD /EHsc /std:c++20 /Zi /Zo /DEBUG:FULL.
No allocator, heap operation, replay lifetime or concurrency code was changed.

## Four modes and fixed launch factors

| Mode | Image | Selector | Observer |
|---|---|---|---|
| original | Unchanged THREAD-06 | off_ (ignored) | No hooks |
| off | Diagnostic | off_ | Mode lookup/no-op hook, no marker or wait |
| marker | Same diagnostic | mark | Five JSON markers and stdout flush, no wait |
| ack | Same diagnostic | ack_ | Five markers/flush and stdin C acknowledgement |

Original SHA-256:
`e29c95e4d4e4bea399fa532683656f8a39f1e5e737a9b09428056feb35b1c77b`.
Diagnostic SHA-256 (same for off/marker/ack):
`2ccdb450cf25b156ba5be0b4e39a8a316b19e1923d120eae073d66640e891540`.
Input SHA-256:
`7571f8b57e92fefbf75954794d6d193b6c4e16c781d21528cc61988ed3e0ab8e`.

The original executable accepts exactly its saved four-argument CLI; an extra
observer argument would fail. Therefore the four-character selector is supplied
through **SE_OBSERVER11**, not an invented command-line option. Original/off have
identical environment hashes. Marker/ack differ only in this four-character
value; the rest is one fixed copied environment. Values are not published.

All modes use the same CWD `C:\Boris\ServerEngineV4`, absolute input/stage paths,
mode single, stdin=PIPE, stdout=PIPE, per-run stderr file and creation flags
0x08000000. Even argv[0] is held constant: Popen's explicit executable/application
name chooses the actual image while its argument list is identical for every
process. Actual module image path and loader/code layout still differ by design
between original and diagnostic; no claim of isolating code layout alone is made.

Balanced schedule (four blocks, each mode once per block and once per position):
original/off/ack/marker; off/marker/original/ack; marker/ack/off/original;
ack/original/marker/off. Four independent processes per mode.

## Capture and validation

All 16 distinct PIDs exited zero. Before/after executable/input/stages/boundaries
hashes match for every run. The original CONTROL-07 validator independently checks
each marker-filtered JSONL: all 274 boundary signatures, drift calculation, zero
physical worker stacks in single and terminal counters (Alloc 9536484, matched
Free 9536471, unmatched Free 159, live 13, logical starts/stops 8781/8781).
The comparator repeats validation before aggregating results and verifies raw
protocol/replay hashes. Derived boundary CSVs and both original protocol JSONL
and marker-filtered replay JSONL are preserved.

Off/original emit zero observer markers. Marker/ack emit exactly baseline, b00,
b01, b02, b03 with the child PID. Only ack receives byte C on stdin. ACK is sent
immediately after parsing/validation; no address-map capture or artificial delay
is inserted. This tests an acknowledgement wait/transport, not ADDRESS-09's full
VirtualQueryEx-duration observer. No Win32 named events are used.

Twelve contract tests PASS, MSVC build PASS, Python syntax PASS. No ETW,
VirtualQueryEx, private heap, global allocator override or heap trim was run.

## Descriptive results

PrivateUsage at b01/b02/b03 is absolute MiB. Quiescent maxima are drift relative
to each process's initialized replay-metadata baseline. Min/median/max:

| Metric | original | off | marker | ack |
|---|---|---|---|---|
| b01 | 153.945 / 153.975 / 155.090 | 153.594 / 154.152 / 154.164 | 153.258 / 154.170 / 155.098 | 153.250 / 154.395 / 156.258 |
| b02 | 149.496 / 149.682 / 150.594 | 149.660 / 149.787 / 149.797 | 149.574 / 149.689 / 149.812 | 149.570 / 149.805 / 150.359 |
| b03 | 149.258 / 149.850 / 151.055 | 150.891 / 150.904 / 152.602 | 149.578 / 150.791 / 150.922 | 150.668 / 150.846 / 151.906 |
| b03 spread | **1.796875** | **1.710938** | **1.343750** | **1.238281** |
| b02→b03 delta | -0.238 / +0.111 / +0.574 | +1.109 / +1.109 / +2.941 | +0.004 / +1.102 / +1.109 | +0.430 / +1.104 / +2.090 |
| Quiescent maximum drift | **54.766 / 60.623 / 62.078** | **14.664 / 22.303 / 22.309** | **22.297 / 23.289 / 65.062** | **22.305 / 24.063 / 26.367** |

Selected-cycle drift medians (MiB):

| Cycle | original | off | marker | ack |
|---|---:|---:|---:|---:|
| 70 | 9.531250 | 12.044922 | 12.041016 | 10.052734 |
| 80 | 7.591797 | 3.724609 | 3.728516 | 3.701172 |
| 90 | 57.693359 | 13.361328 | 13.609375 | 13.599609 |

Median contrasts, right minus left (MiB):

| Contrast | b03 | b02→b03 delta | Quiescent maximum |
|---|---:|---:|---:|
| off − original | +1.054688 | +0.998047 | **-38.320312** |
| marker − off | -0.113281 | -0.007812 | +0.986328 |
| ack − marker | +0.054688 | +0.001953 | +0.773438 |

The off maximum is lower than original in all four corresponding blocks, but
marker/off and ack/marker block differences vary substantially. Raw block
differences and all per-process results are in comparison.json. Four samples per
mode do not justify an equivalence claim or a sole-mechanism attribution.

## Interpretation and decision

The largest observed contrast is **original image versus diagnostic image with
mode lookup/no-op hooks**, not marker output or acknowledgement median differences.
This is evidence to investigate diagnostic build/module/code-layout plus hook
effects as a combined factor. It does not prove ASLR, LFH, argv, mode lookup or
any specific allocation owner caused the difference.

All four modes still have >1 MiB b03 spread. Individual address maps therefore
cannot establish a reliable cause without controlling or attributing initial
heap state. Marker and ACK effects are not ruled out merely because their median
contrasts are smaller. Historical CONTROL-07/ADDRESS-09 runs also used different
I/O protocols and binaries; the new experiment must not be treated as a direct
reconstruction of either historical environment.

No mode reproduces >128 MiB drift. No private-domain candidate, new concurrency
model or production allocator change is accepted. Further work must resolve the
original/off image+no-op contrast and initial-state variability before attributing
maps or claiming that pauses alone explain earlier results.

Frozen OFF/USE/final PGD hashes verified unchanged. Original replay unchanged.
PGO OFF; no commit, push or deployment. Release BLOCKED; NO ADOPTION.

## Evidence

Requested result: `build/project-unload-observer-11/run-01/comparison.json`.
capture.json records exact application names/argv, CWD, selectors/environment
digests, PIDs, hashes, validation/counters and marker counts. Each run preserves
protocol JSONL, replay JSONL, CSV and stderr. Source identity/build logs are in
the parent build folder. Sources: `experiments/project-unload-observer-11/`.
