# SOURCE-V2-CRITICAL-01 — current V2 CPU sampling and one bounded candidate

2026-10-26. **Diagnostic completed. One candidate tested; NO ADOPTION.**
Production remains current Header V2 / Source V2, Fast Reader ON, Batch ON,
existing String Path Reuse, RAM G and packed persistence. No production source
or configuration was changed. The prior STRING-BATCH result confirms an already
active optimization; see `SOURCE_STRING_BATCH_AB_01.md`.

## Capture is current, optimized and unsandboxed

Fresh unmodified source snapshot at `build/source-v2-critical-01/source`, Git
base 87abe00, complete source manifest. Pre-existing local PGO options and
resolver audit definitions are present but inactive: PGO OFF, resolver audit OFF.
MSVC 19.51 x64 /O2 /GL /LTCG, IPO ON, /Zi /Zo, /DEBUG:FULL. No hot-path probes,
alternative Parser, semantic model, new cache or format changes in this binary.
The target is the production-code **benchmark**, not the TCP server executable.

Ordinary unsandboxed benchmark processes remain medium-integrity. A separate
elevated WPR helper records only its uniquely named sessions; it never elevates
the measured processes or cancels someone else's recorder. Four blocks each
have eight sampled and eight control PUBLISH calls, reversing sampled/control
order between blocks. Two warmups precede the 64 recorded calls. The exact
original C9 UnitProXL fixture and ordered project.json bytes are unchanged.

CPU.Verbose.File captures SampledProfile stacks at the recorded 1 ms interval.
CSwitch events are **not** counted as CPU samples. No builds or analysis run
during capture. No samples/timing outliers are removed. All compiled/runtime
outputs match the existing common hashes. Matching frozen pair:

- EXE SHA-256 `055e0540824f7a20f4cb2beccf8a216e04f3ef5390934e75ddaba3d34a7f5255`
- PDB SHA-256 `f510f5d86d8ad1772b431f845f264be41c860b6c17e3e5ed70079959eddfe8dc`
- GUID `08becb47-8c37-488a-a325-5d322aa13aa7`, age 1

EXE/PDB and complete source snapshot are reverified after all work. The initial
in-sandbox /Zi build hit C1902 (PDB manager); the successful build ran outside
sandbox. No IPO-off diagnostic executable substitutes for production code.

## Trace quality and observer cost

174774 target-process CPU samples, 14249 recognizable Source samples across all
four blocks (3604 / 3621 / 3482 / 3542). Events/buffers lost: zero. Truncation:
false. Unresolved application samples: zero. Missing stacks: 126, 0.0721% of
target samples; if all belonged to Source, 0.8843% of recognized Source count.
Unresolved system leaf samples within recognized Source: 106 (0.744%).

Windows public PDBs are downloaded from the official Microsoft symbol server
into this experiment's workspace. GUID and DBI age match the images, with PDB
info age at least the image age, following
[Microsoft's native PDB validation](https://github.com/microsoft/microsoft-pdb/blob/master/PDB/dbi/pdb.cpp#L783).
The stricter equal GUID/age check for our private EXE/PDB remains unchanged.
The download manifest records identities and any unavailable system PDBs.

| Mean wall-time | Control | Sampled | Paired block-mean difference |
|---|---:|---:|---:|
| Full PUBLISH | 1308.80 ms | 1384.99 ms | +76.19 ms |
| Source | 436.83 ms | 445.69 ms | +8.86 ms |

Sampling is not free; most observed total overhead is outside Source, including
lexical preparation (+46.83 ms). Do not rank performance by profiled elapsed
times or subtract observer overhead from individual function estimates.

## Source CPU distribution

Inclusive CPU sample weights per sampled PUBLISH (32 launches; PID reuse is
handled by counting launches, not distinct PID integers):

| Recognizable context | Samples | Source CPU share | Estimated CPU ms/PUBLISH |
|---|---:|---:|---:|
| read_endpoint and callees | 3976 | 27.90% | 124.25 |
| Graph commit and index helpers | 3361 | 23.59% | 105.03 |
| Source token handling | 2999 | 21.05% | 93.72 |
| string_table / identity_space | 2488 | 17.46% | 77.75 |
| insert_link_target_index context | 919 | 6.45% | 28.72 |
| Kernel Memory Manager / page-fault context | 725 | 5.09% | 22.66 |
| User heap context | 501 | 3.52% | 15.66 |

**Inclusive rows overlap and must not be summed.** Index insertion is part of
Graph commit; token and intern operations can be inside read_endpoint. Kernel
MM is already included in Source CPU. It is not off-CPU waiting or the whole
430 ms Source duration. CPU samples do not measure sleeping/waiting time.
Recognized Source CPU weight averages 445.28 ms in the sampled series; Source
wall averages 445.69 ms, without implying an exact accounting identity.

Concrete functions, observed leaf versus inclusive weights:

| Function | Own CPU ms/PUBLISH | Inclusive CPU ms/PUBLISH |
|---|---:|---:|
| read_endpoint | 29.53 | 124.25 |
| string_table::intern_prehashed | 33.91 | 49.81 |
| graph::insert_link_target_index | 28.41 | 28.72 |
| graph::add_resolved_link | 0.31 | 42.06 |

IPO/ICF warning: a prominent leaf is named vector<link_record>::push_back, but
observed callers attribute 857 of these leaf samples to endpoint path interning,
and hundreds to resolver path construction. Only 78 have add_resolved_link as
the immediate caller. Do not call this a 40 ms link-vector bottleneck based on
the folded symbol name. Raw callers and unresolved leaves are retained. No
hidden inline frames are invented.

## Isolated confirmation of Graph link commit

Replay actual 367144 persisted links, in their original slot order, through
the actual public production rebind_resolved_link entrypoint on fresh targets.
Use the same optimized production object files; no private Graph-access hack
or replacement index algorithm. Only link storage/index is populated in this
diagnostic replay, not a second complete semantic Graph. All returned slots,
target lookups, counts and checksum are verified outside timing.

Two warmups + 32 recorded repeats, outside sandbox. Median full link-commit
loop wall-time **18.42 ms**, range 17.46–21.22 ms; median thread cycles 80514060.
Clocks are around the whole loop, not each link. Allocation/index growth is in
the loop; verification, file access and Graph destruction are outside it.
Per-repeat GetThreadTimes CPU accounting is quantized and is not precise
user/kernel attribution at this duration.

This replay has contiguous warmed inputs and a much smaller active working set
than full PUBLISH. It confirms a concrete component cost, not a prediction of
42 ms savings or a pure insert-loop duration. Initial add_resolved_link-only
replay is retained separately, not substituted for the production entrypoint.

## One candidate: eliminate the second new-target lookup

On a fresh target, rebind_resolved_link searches the target index, then delegates
to add_resolved_link, which repeats that search. The private candidate skips
only the latter lookup, using the **exact original append tail**. Existing
target replacement, validation, capacities, link IDs, warning policy and data
formats are unchanged. No cache, Semantic Model or alternate pipeline.

Same executable selects baseline/candidate; the other optimized production
objects are reused. Sorted diagnostics, artifact hashing and optional SHM
hashing are outside timing. Compiled-project Graph tests pass in both modes;
78 functional operations match full compiled/runtime/SHM output and diagnostics,
including replacements, references, rebindings and invalid inputs.

15 alternating native pairs after warmup, ETW stopped, unsandboxed:

| Native metric | Baseline | Candidate |
|---|---:|---:|
| Full PUBLISH median | 1289.13 ms | 1282.97 ms |
| Peak working-set median | 2574499840 B | 2574471168 B |

Median paired gain **11.51 ms**, 95% bootstrap CI **−8.18 to 24.44 ms**,
wins 11/15. **The full-PUBLISH acceptance criterion fails: CI includes zero.**
No significant memory result either. No extra timing pairs were added to chase
significance. Absolute times are not compared with a different capture series.

Seven separate phase-only pairs show Source median 422.270 → 417.823 ms;
paired Source gain 5.782 ms, CI 2.245–11.052 ms, wins 6/7. However profile full
PUBLISH paired gain is 0.56 ms, CI −17.78–26.05 ms. Source-only improvement is
insufficient for adoption. Do not attribute the full 28 ms insertion cost to the
duplicate lookup: insertion remains, and find_link_target own sampled CPU is
only about 7 ms across both lookups.

All 44 timed candidate operations and pre/post PUBLISH/LOAD/audit checks match
the common compiled/runtime and logical SHM hashes. Inputs, binaries and candidate
sources remain unchanged; original fixture artifacts are restored. The candidate
exists only in the isolated build; production needs no rollback because it was
never modified. Native default production retains both original lookups.

## Decision and retained evidence

Keep production V2 unchanged. Retain the accepted existing String Batch ON.
The endpoint/token/intern path is the largest measured broad CPU envelope;
the narrowly tested link-lookup shortcut does not establish a full PUBLISH win.
No further algorithm/cache experiment was started.

`build/source-v2-critical-01/`: source manifest, frozen EXE/PDB, four ETLs,
capture/measurements.csv, xperf loss checks, real sampled stacks and callers,
system-symbol manifest, critical-summary.json, replay raw data, candidate source,
native/profile samples, correctness logs, candidate-results/summary.json and
final-verification.json. WPR reports no active recording after completion.
Reproduction: `experiments/source-v2-critical-01/README.md`.
