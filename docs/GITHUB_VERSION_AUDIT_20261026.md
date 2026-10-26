# GitHub version audit — 2026-10-26

Follow-up: the historical/current absolute timing discrepancy is largely
reproduced by sandbox versus unsandboxed tool execution. Unsandboxed A/B/C finds
historical V2 and modern metadata-reader control effectively equal; fast reader
improves modern PUBLISH by about 3%. See `docs/PUBLISH_UNITPROXL_ABC_01.md`.
Findings below describe the earlier version audit, not a causal explanation
of the cross-environment timing difference.

Repository: https://github.com/proninb/ServerEngineV4

Scope: all remote branch heads and tags advertised by `git ls-remote --heads
--tags origin`, plus local Git objects for their history and relevant source
differences. Seven branches, zero tags. GitHub Releases were not enumerated.
This is a source/history audit, not a benchmark of every historical commit.
No checkout, reset, merge, cherry-pick or production source edit was performed.

## Published branch heads

| Branch | Commit | Commit date | Relationship to current HEAD |
|---|---|---|---|
| `codex/publish-pls09-checkpoint` | `2ed70d627e3650a00f8d697745b053986a8f40f2` | 2026-10-25 | Current HEAD; PLS10 checkpoint |
| `main` | `bbc168b31453eb68dddb3fa749e1c5b6a788184d` | 2026-09-29 | Ancestor, 128 commits behind HEAD |
| `work/header-v2-cursor-opt-17` | `3ca87dc75c00fadb8e366dc209c42f311649c7a8` | 2026-10-13 | Ancestor, 9 commits behind HEAD |
| `work/ic-catalog` | `93df649a5b90b3a1166b566f27fe3e5dad3e29f8` | 2026-10-04 | Ancestor, 54 commits behind HEAD |
| `work/parser-split-v1` | `4abbd9c7d62a7e0a1a351f2e5b2fd0b99bdc0dd7` | 2026-10-22 | Ancestor, 2 commits behind HEAD |
| `work/publish-v2-core-01` | `a13bda92f613381599b3a702735c734aa8a0176c` | 2026-10-18 | Diverged: HEAD has 3 unique commits; this branch has 2 |
| `work/runtime-shm-v2` | `8bb11c4d1ed86fe598c762710edac00672cd4989` | 2026-10-06 | Ancestor, 32 commits behind HEAD |

Local branch names can mislead: local `main` is `26a7a2c` (October 15), while
GitHub `main` is `bbc168b` (September 29). Local `work/publish-v2-core-01` is
`d0e8c72`; the GitHub branch has two additional commits. Compare SHAs, not names.

## Relevant source history

```text
d0e8c72  Source V2 string path handle reuse
  |\
  | caa5012  Skip synchronous mmap flush during PUBLISH
  | a13bda9  PUBLISH-FAST-ACQUIRE-01
  |
  4abbd9c  PUBLISH optimizations / parallel Header experiments
  2fe4827  PUBLISH V2 / PLS02–PLS09 checkpoint
  2ed70d6  PLS10 checkpoint (current HEAD)
```

Neither `caa5012` nor `a13bda9` is an ancestor of HEAD. This does **not** mean
all their behavior is missing: later checkpoints independently contain related
changes.

| Behavior | `a13bda9` branch | Current HEAD |
|---|---|---|
| PUBLISH durable flush | Disabled through preceding commit | Disabled (`persistence.durable = mode != publish`) |
| Content SHA-256 during normal PUBLISH acquisition | Omitted | Disabled by `configure_publish_acquisition()` |
| USN/change token capture during PUBLISH | Omitted | Disabled by `configure_publish_acquisition()` |
| Source byte ownership | Separate owned strings, move adoption | Separate owned strings, move adoption |
| Windows acquisition | `acquire_file_content_fast`: CreateFile / GetFileSizeEx / ReadFile | `acquire_windows_snapshot`: metadata before and after ReadFile, plus last_write_time |
| Windows share mode | Read / write / delete sharing | Read sharing |

Relevant current files:
`server_engine/project/file/file_context.cpp`,
`server_engine/project/file/file_identity.cpp`,
`server_engine/project/project_full_construction.cpp`.

The acquisition difference is concrete, but its performance impact has not been
measured in a controlled comparison here. The two branches also differ in
parser/runtime work; comparing whole branch binaries would not isolate it.
The sharing and consistency behavior differs as well, so blindly cherry-picking
the older fast path is not justified by this audit.

## 1356.49 ms versus 1840.59 ms

These are benchmark results, not Git tags or release version numbers.

| Observation | Evidence |
|---|---|
| Historical V2 median 1356.49 ms | PLS10-C9, 15 native pairs; `build/parallel-local-source-10-c9/ab-01/summary.json` |
| Current pipeline control median 1840.59 ms | Compact pipeline experiment; `build/publish-pipeline-01-compact/summary.json` |
| Historical acquisition/lexical profile median | 133.847 ms |
| Current control acquisition/lexical profile median | 573.885 ms |
| Unchanged historical executable, original fixture, rerun native | 1885.50 / 1885.42 / 1850.55 ms; median 1885.42 ms |

The historical executable's SHA-256 is
`841c7848aaf4631fef25c8bf6eb28de7cac072f9b01dd66dcf0cae7b0a1adde5`.
The rerun preserves this hash and reproduces the historical compiled/runtime
artifact hashes. Details: `build/pipeline-baseline-diagnosis-01/` and
`docs/PUBLISH_PIPELINE_01.md`.

Thus the cross-series difference alone does not establish a code regression.
The GitHub fast-acquisition branch is a separate source finding, not a proven
explanation for the historical timing difference. There is no established
CPU/storage/antivirus/cache cause yet. No new timing runs were made for this
GitHub audit.

The recent direct-G-mmap and PUBLISH-PIPELINE reports/experiments are local
untracked artifacts, not additional published GitHub heads. The active source
remains the RAM-G/packed-encoding path after the direct-mmap rollback.

## Decision supported by this audit

Do not roll back to GitHub `main` on the strength of these two absolute timings:
it is an older runtime checkpoint. Keep the frozen historical V2 executable as
the timing control. To assess acquisition changes, use an isolated same-build
A/B of the two acquisition paths with matching consistency requirements, input,
settings and alternating runs. This audit did not implement that experiment.
