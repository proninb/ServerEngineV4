# PROJECT-UNLOAD-CONTROL-STARTUP-08

2026-10-29. **Offline first-divergence analysis COMPLETE / cause OPEN /
NO ADOPTION / release BLOCKED.** No new replay, argv A/A, process capture,
VirtualQueryEx run, allocator change or production rebuild.

The described ZIP was not attached or found locally. A local read-only analyzer
was matched to the actual derived CSV schema:
`boundary,cycle,stage,private_bytes,drift_bytes,active_workers,handles,live`.
Unsupported headers cause failure rather than guessed column mapping.

## Evidence checks

All nine original JSONL stdout hashes match the CONTROL-07 manifest. Every CSV
numeric sample is checked against its original JSONL. All 274 boundary sequences
match between runs (ID, cycle, stage, active logical workers, live blocks), and
all terminal counters match: Alloc 9536484; matched Free 9536471; excluded unmatched
Free 159; live 13; logical starts/stops 8781/8781. Each archived run's before/after
executable/input hashes agrees with the common manifest.

Limitation: cumulative allocation/free counters were not logged at each boundary.
Only terminal counters and boundary live/active counts can be verified. This
analysis does not invent per-stage allocation counters.

Ten contract tests PASS. The initial schema test encountered a sandbox temp-folder
permission error; replacing its filesystem fixture with an in-memory CSV fixture
removed that dependency. The complete ten-test suite then passed.

## Earliest observed spread

The 1 MiB threshold is strictly diagnostic and does not change the release gate.
Both absolute Private Bytes spread and baseline-adjusted drift spread first cross
it at **boundary 3, cycle 0, after_unload_2**.

| Boundary | State | Private Bytes spread |
|---|---|---:|
| Initial sample, outside boundaries | Before first checkpoint | 20 KiB |
| 0 | Before first PUBLISH | 16 KiB |
| 1 | cycle 0 after PUBLISH | 0.5390625 MiB |
| 2 | cycle 0 after first UNLOAD | 0.23046875 MiB |
| **3** | **cycle 0 after second UNLOAD** | **2.01171875 MiB** |

At boundary 3 the absolute values range from **156262400** to **158371840 bytes**
(difference **2109440 bytes**). Drift spread is **2117632 bytes / 2.01953125 MiB**.
Runs 3/4 show roughly 4.20 MB baseline-relative drift here versus about 2.1 MB for
most others; run 7 is intermediate. These are differences in PrivateUsage, not
maps identifying committed heap regions.

The first threshold-crossing transition is boundary 2 → 3:
**after first UNLOAD → LOAD/native verification → second UNLOAD**. Deltas are
approximately -0.43 MiB in most runs, +1.35 MiB in runs 3/4, and -0.043 MiB in run 7.
The inter-run delta range is **1871872 bytes / 1.78515625 MiB**.

This localizes the first measured >=1 MiB divergence to the first reload/unload
interval. It does not establish that UNLOAD itself, LOAD, a particular allocation,
or initial address layout caused it. Smaller differences already exist earlier;
the threshold crossing is not necessarily the first internal causal difference.

## Largest differential transitions

These rank **inter-run spread of the transition delta**, not maximum absolute
footprint or execution time:

| From | To | Delta spread, MiB |
|---|---|---:|
| cycle 89 after UNLOAD2 | cycle 90 after PUBLISH | 61.355469 |
| cycle 75 after UNLOAD2 | cycle 76 after PUBLISH | 26.746094 |
| cycle 58 after UNLOAD2 | cycle 59 after PUBLISH | 18.238281 |
| cycle 79 after UNLOAD2 | cycle 80 after PUBLISH | 13.472656 |
| cycle 54 after UNLOAD2 | cycle 55 after PUBLISH | 12.636719 |

`first-divergence.json` contains all boundary spreads, all per-run values and all
273 ranked transitions. A later large delta cannot replace the earliest divergence
as the first localization target.

## Next decision

Do not begin a concurrency experiment or private allocation domain. If proceeding
to controlled address diagnosis, preserve argv/environment first and inspect the
**first cycle's LOAD → second UNLOAD interval**, with an intermediate post-LOAD
checkpoint. Capture actual committed/reserved regions separately from PrivateUsage.
Include the initial/pre-PUBLISH snapshots to test whether earlier sub-threshold
layout differences predict the later split. The optional relative/absolute argv
ABBAABBA series was not run and its effect remains unmeasured.

Startup heap/layout, LFH, ASLR, scheduling and memory-manager explanations remain
hypotheses. This offline data identifies an interval, not an allocation owner or
physical decommit mechanism. No release criterion was weakened.

Production C++, allocator, UNLOAD, TCP and PGO unchanged. OFF/USE/final PGD hashes
verified unchanged. No commit, push or deployment. PGO OFF; release BLOCKED.

## Output and reproduction

Result: `build/project-unload-control-startup-08/first-divergence.json`.
Sources: `experiments/project-unload-startup-08/first_divergence.py` and
`test_divergence.py`. Source run directory remains unmodified.

The analyzer creates a new output file and refuses to overwrite existing evidence.
The README records the actual CLI, without relying on an unavailable schema file.
