# PROJECT-UNLOAD-CONTROL-STABILITY-07

2026-10-29. **Measurement COMPLETE / shared-control footprint not repeatable /
NO ADOPTION / release BLOCKED.** Nine independent processes; no rebuild, server
launch, new ETW capture, concurrency or private-domain experiment.

The ZIP mentioned in the request was not attached or present locally. The actual
saved THREAD-06 executable uses binary input and JSONL stdout, not the proposed
kit's TSV/CSV CLI. It was used unchanged. CSV files are derived from its original
JSONL, which is also preserved.

## Fixed inputs and validation

Executable: `build/project-unload-thread-06/replay.exe`.
SHA-256 **e29c95e4d4e4bea399fa532683656f8a39f1e5e737a9b09428056feb35b1c77b**.

Input: `build/project-unload-thread-06/strict-replay.bin`, 916365600 bytes.
SHA-256 **7571f8b57e92fefbf75954794d6d193b6c4e16c781d21528cc61988ed3e0ab8e**.

Executable, input, stages.bin and boundaries.json were SHA-256 checked before
and after **each** process. All hashes match. All nine exit codes are zero;
nine distinct PIDs are recorded. Each run has all **274 boundary signatures**
matching THREAD-06 (boundary ID, stage code, active logical workers, live block
count), and stages are independently checked against stages.bin.

Each final record matches Alloc 9536484, matched Free 9536471, excluded unmatched
Free 159, live blocks 13, logical starts/stops 8781/8781. Physical worker stack
commit is zero in single. Every drift sample is checked against its own initial
Private Bytes. Seven Python contract tests and syntax checks PASS.

## Nine independent results

All values below are quiescent post-first-UNLOAD drift in MiB, relative to each
process's initialized-metadata baseline.

| Run | PID | cycle 70 | cycle 80 | cycle 90 | Maximum, all cycles | Maximum cycle |
|---|---:|---:|---:|---:|---:|---:|
| 1 | 5736 | 7.128906 | 3.367188 | 3.585938 | 16.039062 | 89 |
| 2 | 8940 | 7.144531 | 3.371094 | 3.578125 | 16.042969 | 89 |
| 3 | 13904 | 15.375000 | 3.656250 | 4.230469 | 27.468750 | 75 |
| 4 | 26664 | 15.371094 | 3.648438 | 4.238281 | 27.464844 | 75 |
| 5 | 27724 | 7.144531 | 3.375000 | 3.589844 | 16.046875 | 89 |
| 6 | 30640 | 7.136719 | 3.371094 | 3.566406 | 16.042969 | 89 |
| 7 | 14568 | 3.929688 | 3.304688 | 64.226562 | 64.226562 | 90 |
| 8 | 25132 | 7.121094 | 3.375000 | 3.589844 | 16.046875 | 89 |
| 9 | 19596 | 7.152344 | 3.371094 | 3.605469 | 16.042969 | 89 |

The six ~16 MiB, two ~27.47 MiB and one ~64.23 MiB maxima occur with the same
executable, inputs and command. Selected-cycle peak is 70 in 8/9 runs and 90 in
1/9. Full-series maximum shifts between cycles 89, 75 and 90.

Important distinction: pairwise signs **70 > 80** and **90 > 80** hold in all nine
runs. Thus this sample does not show a repeated 70-versus-80 sign reversal within
the nine processes. Nevertheless, peak placement and amplitude are not repeatable.
The phase descriptor in comparison.json includes the selected peak/trough cycle
as well as those two signs; it finds two patterns. No arbitrary absolute "high"
threshold was introduced to obtain that result.

## Historical control comparison and limits

DOMAIN-04 shared selected values were 64.344 / 12.699 / 59.668 MiB;
the original THREAD-06 single values were 12.426 / 61.199 / 10.754 MiB.
Neither historic selected-cycle pattern is reproduced quantitatively by a
stable nine-process template. None of the new runs exceeds 128 MiB drift.

There is a known launch difference relative to the historic THREAD-06 command:
CONTROL-07 supplies **absolute** input/stage path arguments; THREAD-06 used
relative arguments. All nine CONTROL-07 processes receive the identical absolute
command. Different argv strings can change startup allocations/layout, so their
effect on cross-series comparison remains unmeasured. This does not explain away
the variation within the nine identically launched processes.

The result establishes observable control-footprint variability, not its cause.
Startup heap/address layout, inherited environment, timing and other heap behavior
were not independently controlled or attributed in this phase. Executable/input
hash equality alone does not establish equivalent initial NT heap state. No
allocator-path ETW or ASLR/address-map inference is claimed.

## Decision

Do not introduce a concurrency model or accept a private allocation domain from
the existing replay comparisons. Investigate shared-control startup heap/layout
repeatability first, including the explicit launch-argument difference, before
attributing later changes to threads or overlapping heap operations.

No production C++, allocator, UNLOAD, TCP or PGO change. OFF/USE/final PGD hashes
remain unchanged. No commit, push or deployment. PGO OFF; release BLOCKED.

## Evidence

Requested result:
`build/project-unload-control-stability-07/run-01/comparison.json`.
The directory contains nine original `run-NN.jsonl` files, nine derived CSVs,
stderr logs, per-run PIDs/exit codes, before/after input hashes and stdout hashes.
The phase summary was refined after measurement to include peak/trough placement;
raw process measurements and executable were not changed or rerun.

Harness and seven contract tests: `experiments/project-unload-control-07/`.
