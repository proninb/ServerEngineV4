# PUBLISH-CRITICAL-PATH-02 — captured and analyzed

Kernel symbol follow-up completed in [RUNTIME-SHM-FAULTS-01](RUNTIME_SHM_FAULTS_01.md).
The historical unresolved-kernel limitation below describes this first export;
the follow-up resolves the same CPU events without another capture.

## Results: capture-20261023-144348

All 98 full PUBLISH calls preserved both golden artifacts. Captured module/PDB
identities match. Eight blocks: 284572 CPU samples, 20503 recognizable Source
samples, 305 missing-stack samples (0.1072% of total; worst case 1.49% of Source),
zero unresolved application frames, zero ETW lost events/buffers, no truncated
traces. Actual interval: 1 ms. Strict zero-missing-stack gate is **not passed**;
broad ranking remains usable with limitations. OS functions remain addresses.

| Coarse interval | Control median | Sampled median |
| --- | ---: | ---: |
| Full PUBLISH | **1274.830 ms** | 1394.815 ms |
| Source acquisition/lexical | 134.386 ms | 236.995 ms |
| Header physical | 64.434 ms | 65.185 ms |
| Header semantic | 201.863 ms | 204.024 ms |
| Source | 417.003 ms | 424.267 ms |
| Persistence | 199.395 ms | 201.932 ms |
| Resident | 190.698 ms | 194.226 ms |
| Transient release | 21.010 ms | 21.337 ms |

Paired block-mean observer deltas: full +123.989 ms, acquisition/lexical +103.194 ms,
Header +2.392 ms, Source +7.487 ms, persistence +3.089 ms, resident +5.604 ms.
These include environmental variation. Do not treat sampled lexical wall time
as ordinary production time. All outliers retained. Nested intervals and medians
do not sum to an exact full-time decomposition. This is not an optimization A/B.

No SHA-256/hash_file_content frames occur in the exported target stacks, agreeing
with the separate zero-call diagnostic in PUBLISH-NO-SHA256-01.

### Whole-operation CPU

Recognizable runtime materialization stacks account for 122505 samples (43.05%
of target samples); Source semantic 20503, Header semantic 8627. These are CPU
counts across threads, **not wall-time ranking**. Concurrent Runtime workers can
accumulate more CPU than a longer serial stage.

| Application function | Exclusive samples |
| --- | ---: |
| shm_type_batch_executor::apply_api_one | 26431 |
| tokenize_impl&lt;lexical_stream&gt; | 8306 |
| Apparent vector&lt;derived_type_record&gt;::push_back | 7298 |
| lexical_stream::append | 4156 |
| compiled_project_view::member_at | 3399 |
| semantic_cursor_v2::bind_current | 2647 |

The STL leaf may be shared by ICF; its name does not establish derived-type
creation. Source leaves remain intern_prehashed 1567, read_endpoint 1354,
insert_link_target_index 1309, assignment 1210, advance 1133. Rejected slot caching
and link growth stay disabled.

### Runtime: application code versus OS work

apply_api_one has 120711 inclusive samples; **93821 have an ntoskrnl leaf**.
26890 have other leaves, including its own 26431 exclusive samples. Much of the
work beneath this function executes in Windows, not its range-check instructions.
Without matching OS symbols, these samples cannot be confidently called page
faults, zero-page work, locking or another Memory Manager path.

All 48 sampled processes have 24 sampled threads under apply_api_one. Median
maximum-thread CPU share 5.08%; maximum/mean sample ratio 1.218. CPU distribution
does not establish per-lane wall imbalance. No evidence to revive cyclic object
chunks or simply add workers.

### Parallel physical-plan barrier

Control medians: type plan 83.186 ms, runtime plan 77.857 ms, combined parallel
stage 83.263 ms. Runtime links 73.478 ms and initializations 55.417 ms also overlap.
Optimizing a shorter branch does not imply the same saving in full PUBLISH.

3399 member_at exclusive samples have callers in runtime append_member (1843),
type builder ensure_named (734), ABI layout resolve_windows_class_record (429),
and others. This supports future investigation of repeated metadata decode,
not automatic creation of another global database or a speed claim.

### Next experiment

**RUNTIME-SHM-FAULTS-01:** resolve dominant kernel stacks beneath object materialization
with matching Microsoft OS symbols. Separate Memory Manager/first-touch work,
actual executor instructions and scheduling. Inspect fault events only if present;
otherwise prepare a narrow diagnostic capture with its own overhead control.
Do not assume the cause from unresolved kernel addresses.

The new evidence does not yet justify a particular executor algorithm. Attributing
the whole inclusive cost to application instructions would repeat the failed
range-cache reasoning. Production remains unchanged after profiling. Target <1 s
requires roughly 275 ms from the complete operation, not necessarily Runtime.

Evidence: `build/publish-critical-path-02/capture-20261023-144348/analysis/`:
cpu-summary.json, full-publish-summary.json, focused-evidence.json, per-block raw
JSONL, symbol metadata and trace-loss logs.

## Preparation history

Fresh complete production MSVC Release + IPO build after PUBLISH-NO-SHA256-01.
Matching /Zi + /DEBUG:FULL PDB, no A/B/trace/audit macros. Accepted Header lazy
state and member index 4x remain. Runtime range memo and early context release are off.

Frozen pair and source/build identity:
`build/publish-critical-path-02/frozen/`. Previous frozen pairs/captures are preserved.
The new launcher selects this pair explicitly, so the old pre-SHA default cannot
be sampled accidentally.

## Native control preflight

Two warmups and eight measured full PUBLISH calls on the original isolated fixture.
All ten calls retained compiled/runtime golden SHA-256. EXE/PDB GUID+age matched;
production binaries contain none of the experimental environment selectors.
Existing tool tests passed 3/3; launcher PowerShell syntax and Python syntax checked.

| Coarse interval | Median |
| --- | ---: |
| Total PUBLISH | 1317.815 ms |
| Initial Source acquisition/lexical | 139.362 ms |
| Header physical | 78.084 ms |
| Header semantic | 205.956 ms |
| Source | 425.306 ms |
| Persistence | 202.284 ms |
| Resident | 193.002 ms |
| Transient release | 22.706 ms |

Nested persistence diagnostics: compiled encode 63.975 ms; physical plans 84.958 ms.
Unaccounted median 0.0037 ms. These descriptive medians do not sum exactly to the
median total. Control timings vary and are not a new paired speedup/regression claim.
Raw data: `build/publish-critical-path-02/control-preflight-20261023/`.

## Capture procedure used

The agent's Windows token is not administrative. Actual ETW capture requires the
same elevated PowerShell used successfully for the previous two captures:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\Boris\ServerEngineV4\experiments\publish-critical-path-02\capture.ps1"
```

Two warmups + 8 alternating control/sampled blocks × 6 processes per mode: 98 calls.
The collector verifies golden artifacts outside each PUBLISH timer, retains all
outliers, owns only its named WPR session and reports observer deltas for all coarse
stages. Source is enabled throughout.

At preparation time no post-SHA CPU distribution was claimed before capture.
The full-operation analysis tool was exercised on the historical pre-SHA trace
only to validate accounting (359920 total samples, 336 missing stacks preserved).

After capture, use the export/analyze commands in
`experiments/publish-critical-path-02/README.md`. Review inclusive/exclusive functions,
real caller context, recognizable stage attribution, losses/stack coverage,
block consistency and sampling overhead. CPU counts are not wall time.
Then select one algorithmic experiment against this refreshed baseline.
