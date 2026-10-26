"""Summarize the controlled compact study; retain the owned-plan pilot."""
from pathlib import Path
import json,statistics,sys
repo=Path(__file__).resolve().parents[2];out=Path(sys.argv[1]).resolve()
j=json.loads((out/'summary.json').read_text());rows=json.loads((out/'samples.json').read_text())
functional=json.loads((out/'functional-results.json').read_text());arms=('baseline','serial','overlap')
def median(corpus,series,arm,metric):return statistics.median(r[metric] for r in rows if r['corpus']==corpus and r['series']==series and r['arm']==arm)
def compare(corpus,series,key,metric='total_ms'):return j['summary'][corpus][series][key][metric]
lines=['# PUBLISH-PIPELINE-01 — experimental, no adoption','',
'**Result:** overlap is implemented and content parity passes, but a reliable full-UnitProXL native PUBLISH win is not established. Active production sources are unchanged: RAM G + packed encoder remain the baseline. Direct mmap v27 is not restored.', '',
'## Controlled compact variant','',
'One prepared Type plan is moved into the execution owner without copying operation vectors. Compact Link/Init columns are encoded once into runtime.bin, attached for execution, and their original producer vectors released before SHM. ABI layout remains immutable. The existing physical-plan lane pool is reused for the two consumers.', '',
'```mermaid',
'flowchart TD',
'  G[Final RAM G and packed compiled.bin] --> P[ABI plus Type and Link/Init plans]',
'  P --> C[runtime.bin prefix and compact Execution columns ready]',
'  C --> W[Write independent ABI and Type columns; bind final runtime image]',
'  C --> S[Private SHM: canonical, mark, objects, links, initializations]',
'  W --> J[Join both consumers]',
'  S --> J',
'  J --> O[Publish Project ownership only on complete success]',
'```','',
'Both link marking and object construction retain the baseline 24 lanes on UnitProXL. Type operations are read from their one owning prepared plan; Link/Init operations are read directly from their final immutable mapped columns. There is no second complete operation model, new semantic cache, parser change or Type-plan preparation during Source.', '',
'Writer read/write set: native ABI/Type plan (read-only), runtime.bin ABI sections 34..43 and Type sections 49..65 (writes). Consumer read set: the same immutable native layout/Type plan plus completed Execution sections 44..48; writes target SHM and separate mutable link-resolution scratch. The directory is finalized before either consumer starts. The consumer never reads incomplete ABI/Type file sections. The producer releases its completed G mapping before opening the resident G mapping; all prepared operations are detached from G.', '',
'## Three arms and measurement protocol','',
'- `baseline`: original sequential persistence, then mmap attach and resident creation.',
'- `serial`: identical prepared-plan reuse and compact-section staging, but the remaining writer and SHM consumer execute sequentially.',
'- `overlap`: the same prepared-plan path with the remaining writer and resident consumer running concurrently.', '',
'One frozen Windows x64 MSVC 19.51 Release `/O2` + IPO `/GL`/`/LTCG` executable is used for all arms; no PGO. Date: 2026-10-26. Two warmups per arm, then 15 rotating/reversed three-arm blocks on each corpus, separately for native `publish` and coarse `publish-timeline`. No concurrent builds/tests during timing; no samples removed. Native resident execution has telemetry disabled. Source, binary and input hashes are rechecked after all runs.', '',
'Bootstrap intervals below use 20,000 pair resamples, seed 1101. Positive paired gain = control faster time minus candidate time, so positive means a candidate win. Absolute timings vary with the host; intervals resample blocks and cannot remove all host drift.', '',
'## Native full PUBLISH','',
'Full UnitProXL: **24,027 types, 137,584 objects, 367,144 links**. Synthetic mixed corpus: 10,001 types, 170,001 members, 20,000 objects, 10,000 links.', '',
'| Corpus | Baseline median, ms | Prepared serial median, ms | Overlap median, ms |',
'|---|---:|---:|---:|']
for corpus in ('synthetic','unitproxl'):
 values=[median(corpus,'native',arm,'total_ms') for arm in arms];lines.append('| '+corpus+' | '+' | '.join(f'{v:.3f}' for v in values)+' |')
lines+=['','| Corpus / comparison | Paired gain, ms | 95% bootstrap interval, ms | Candidate wins |','|---|---:|---:|---:|']
for corpus in ('synthetic','unitproxl'):
 for key in ('baseline->serial','baseline->overlap','serial->overlap'):
  m=compare(corpus,'native',key);lo,hi=m['bootstrap95_gain'];lines.append(f"| {corpus}: {key} | {m['paired_gain']:+.3f} | {lo:+.3f} to {hi:+.3f} | {m['candidate_wins']}/15 |")
lines+=['',
'The full-corpus overlap-vs-baseline interval includes zero, as does overlap-vs-serial. Therefore neither the overall pipeline nor the isolated overlap contribution has a reliable native full-corpus benefit in this trial. Marginal medians and medians of paired differences need not subtract to the same number. The tiny synthetic signal is insufficient for full-project adoption.', '',
'## What can actually overlap','',
'UnitProXL coarse-profile medians for the overlap arm:', '', '| Interval | ms |', '|---|---:|']
for label,key in [('Runtime image sizing/allocation/prefix before fork','pipeline_image_setup_ms'),('Compact Execution encode/attach/release before fork','pipeline_compact_setup_ms'),('Remaining independent ABI/Type writer','pipeline_write_ms'),('Resident worker, including checks and SHM','pipeline_resident_ms'),('Intersection of writer and resident-worker intervals','pipeline_overlap_ms')]:
 lines.append(f"| {label} | {median('unitproxl','profile','overlap',key):.4f} |")
lines+=['',
'The overlap metric starts when the resident worker begins opening/checking G; it is **not** an isolated SHM-materialization overlap measurement. The remaining writer is only about 1.76 ms. Encoding dependent execution sections and initializing the runtime file still precede the fork. This slice cannot justify assuming a 195 ms saving from the whole Resident stage.', '',
'Profile timings are diagnostic, not the native latency baseline. Attach/preparation instrumentation differs when a prepared plan bypasses persisted attachment. Also, pipeline `persistence_ms` already includes the joined resident worker; adding `pipeline_resident_ms` would double count overlapping work. Only `persistence_ms + resident_ms` (resident_ms is zero on the prepared path) is a comparable combined wall interval.', '',
'| UnitProXL profile metric | Baseline | Prepared serial | Overlap |','|---|---:|---:|---:|']
for label,key in [('Persistence + resident wall, ms','persistence_resident_ms'),('Link mark lanes','pipeline_mark_lanes'),('Object lanes','pipeline_object_lanes')]:
 vals=[median('unitproxl','profile',a,key) for a in arms];lines.append('| '+label+' | '+' | '.join(f'{v:.3f}' for v in vals)+' |')
lines+=['', '## Memory and files','', '| UnitProXL native metric | Baseline | Prepared serial | Overlap |','|---|---:|---:|---:|']
for label,key in [('Peak working set, MB','peak_ws_bytes'),('Peak private commit, MB','peak_private_commit_bytes'),('compiled.bin, MB','compiled_bytes'),('runtime.bin, MB','runtime_bytes')]:
 vals=[median('unitproxl','native',a,key)/1e6 for a in arms];lines.append('| '+label+' | '+' | '.join(f'{v:.3f}' for v in vals)+' |')
lines+=['',
'MB = 1,000,000 bytes. Peak private commit uses Windows PeakPagefileUsage; working set includes file mappings and SHM and is not interchangeable with private commit. File sizes and complete file bytes match across arms. Peak working set increases by roughly 32 MB in the overlap arm; private commit is essentially unchanged.', '',
'## Correctness, dependencies and failures','',
f'- {len(functional)} functional checks cover enum/inheritance, scalar/extern completion, char arrays, nested objects/arrays, repeated final reference binding and constructors; parser/extern errors, reserved System and reference cycles; serial fallback; and injected plan, file creation, map, ABI/Type/Execution, compact setup, resident, post-SHM and pre-publication failures.',
'- Failure checks run a same-process probe: output must remain null, all artifacts must be absent, and the same named SHM at the same fixed VA must be creatable again. Post-SHM/pre-publication failures require that the private consumer actually completed successfully first. This checks release before process exit, not merely OS cleanup on exit.',
'- All timing runs compare SHA-256 of the complete compiled.bin and runtime.bin outside the operation timer. Warmup PUBLISH, cold audit and repeat LOAD compare raw SHM SHA-256 across every arm before/after each series. Cold audit calls verify_contents().',
'- Publication is a private staged Project pointer until both consumers join, runtime.bin structural bind succeeds and SHM finishes. No partially built Project pointer reaches the caller. On either consumer failure, staged ownership is destroyed before the existing artifact cleanup.',
'- Fixed SHM VA/range checks and canonical -> mark -> objects -> links -> initializations order are unchanged. No SHM relocation after native references are installed. Since G is final before the experiment starts, subsequent Source writes cannot invalidate early materialization.',
'- REBUILD and LOAD never opt into the staged construction path. PUBLISH durable flush remains disabled. No change is made to file formats, parser dispatch, WHO/WHERE numbering or SourceMap.', '',
'## Retained first pilot (owned execution)','',
'The first single-plan implementation used the existing owned-plan executor, which falls back to one link-mark lane rather than baseline 24. It passed 71 functional checks, exact file/SHM parity and 180 timed observations, but was not a clean concurrency-only comparison. Its full UnitProXL native baseline->overlap paired gain was -16.70 ms (95% interval -46.28..-4.43 ms). It is retained under `build/publish-pipeline-01-final/` and is not silently excluded or mixed with the controlled compact results.', '',
'The controlled compact variant above restores the identical 24-lane link marking and object executor and adds a matched serial control. No further architecture changes or Source-overlap stage were attempted after the controlled result failed to establish a full-corpus win.', '',
'## Artifacts and reproduction','',
'Implementation/reproduction instructions: `experiments/publish-pipeline-01/README.md`.', '',
'Controlled data: `build/publish-pipeline-01-compact/`:', '',
'- `summary.json`, `samples.json`, `samples.csv`, `run.log`, `timing-logs/`: all 180 timed observations, intervals and stdout/stderr.',
'- `functional-results.json`, `functional.log`, `functional-logs/`, `checks.json`, `parity.json`: functional/failure checks, 36 warmup/audit/LOAD checks and hashes.',
'- `source/`, `source-manifest.json`, `snapshot.json`, `build*.log`, `binary.json`, `binaries/PublishPipeline01.exe`: exact source/build/binary provenance.',
'- `input-manifest.json`, `host.json`: common immutable input hashes and host information.', '',
'Active engine/CMake tracked changes remain exactly the pre-existing production-PGO CMake block and Source endpoint resolver edits. All pipeline code lives under the standalone experiment/private generated sources; there is no default production enablement to roll back.', '']
text='\n'.join(lines);(repo/'docs/PUBLISH_PIPELINE_01.md').write_text(text,encoding='utf-8');(out/'report.md').write_text(text,encoding='utf-8')
print(text[:2100])
