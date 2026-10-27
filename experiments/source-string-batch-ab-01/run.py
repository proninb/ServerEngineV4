"""Unsandboxed native A/B; a separate binary supplies Graph-operation counts."""
from pathlib import Path
import hashlib,json,os,random,shutil,statistics,subprocess,sys
repo=Path(__file__).resolve().parents[2]; out=Path(sys.argv[1]).resolve()
assert out.is_relative_to(repo/'build')
logs=out/'timing-logs'; logs.mkdir(exist_ok=False)
bins=out/'binaries'; bins.mkdir()
native=bins/'StringBatchNative.exe'; audit=bins/'StringBatchAudit.exe'
for src,dst in [('ServerEngineV4PublishBenchmark.exe',native),('ServerEngineV4StringBatchAudit.exe',audit)]:
    shutil.copy2(out/'build/Release'/src,dst)

def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
hashes={p.name:sha(p) for p in (native,audit)}
assert b'graph_upserts' not in native.read_bytes(),'native contains audit instrumentation'
fixture=repo/'build/parallel-local-source-10-c9/ab-01/fixture'; project=fixture/'project.json'
manifest={p.relative_to(fixture).as_posix():sha(p) for p in sorted(fixture.rglob('*')) if p.is_file() and '.serverengine' not in p.relative_to(fixture).parts}
(out/'input-manifest.json').write_text(json.dumps(manifest,indent=2))
(out/'binary.json').write_text(json.dumps(dict(hashes=hashes,execution='unsandboxed require_escalated caller',
    configuration='MSVC Release /O2 /GL /LTCG, PGO OFF, current V2 + production Fast Reader',
    project=str(project),native_audit_disabled=True),indent=2))
base={k:v for k,v in os.environ.items() if not k.startswith(('CW_','PLS','FAST_ACQUIRE_','PUBLISH_PIPELINE_','SOURCE_STRING_BATCH_'))}
artifact=out/'artifacts/common'; gold=None; shm=None; samples=[]; checks=[]; counts={}

def fields(text):
    result={}
    for line in text.splitlines():
        for p in line.split(','):
            if '=' not in p:continue
            k,v=p.split('=',1)
            try:result[k]=float(v)
            except ValueError:result[k]=v
    return result

def run(mode,operation,tag,counter=False,shm_audit=False):
    global gold,shm
    env=dict(base,SOURCE_STRING_BATCH_01_MODE=mode,FAST_ACQUIRE_02_ARTIFACT_ROOT=str(artifact))
    if shm_audit:env['CW_RUNTIME_SHM_AUDIT']='1'
    command=[str(audit if counter else native),operation,str(project),'24027']
    r=subprocess.run(command,env=env,cwd=repo,capture_output=True,text=True,timeout=240)
    label=f'{tag}-{mode}-{operation}'
    (logs/(label+'.stdout')).write_text(r.stdout); (logs/(label+'.stderr')).write_text(r.stderr)
    assert r.returncode==0,(label,r.stderr[-4000:])
    row=fields(r.stdout)
    assert (row['types'],row['objects'],row['links'])==(24027,137584,367144)
    payload={n:sha(artifact/n) for n in ('compiled.bin','runtime.bin')}
    if gold is None:gold=payload
    assert payload==gold,(label,'artifact parity',payload,gold)
    if shm_audit:
        digest=row['shm_audit_sha256']
        if shm is None:shm=digest
        assert digest==shm,(label,'SHM parity')
    if counter:
        assert row['batch_mode']==int(mode=='batch')
        assert (row['string_statements'],row['string_elements'],row['string_upserts'])==(15899,508768,508768)
        assert row['string_batch_calls']==(15899 if mode=='batch' else 0)
        assert row['string_scalar_calls']==(0 if mode=='batch' else 508768)
    else:assert 'graph_upserts' not in row
    row.update(mode=mode,operation=operation,tag=tag,command=command)
    print(label,row['total_ms'],'ms',flush=True)
    return row

for mode in ('scalar','batch'):
    counts[mode]=run(mode,'publish','counter-pre',True,True)
    for operation in ('publish','audit','load'):checks.append(run(mode,operation,'pre',False,True))
for mode in ('batch','scalar'):checks.append(run(mode,'publish','warm'))
for series,operation,n in (('native','publish',15),('profile','publish-timeline',7)):
    for pair in range(n):
        for mode in (('scalar','batch') if pair%2==0 else ('batch','scalar')):
            row=run(mode,operation,f'{series}-{pair:02}')
            row.update(series=series,pair=pair); samples.append(row)
        (out/'samples.json').write_text(json.dumps(samples,indent=2))
for mode in ('batch','scalar'):
    for operation in ('audit','load'):checks.append(run(mode,operation,'post',False,True))
    after=run(mode,'publish','counter-post',True,True)
    assert {k:v for k,v in after.items() if k.startswith(('string_','graph_','index_','batch_mode'))}=={
        k:v for k,v in counts[mode].items() if k.startswith(('string_','graph_','index_','batch_mode'))}

summary={}
for series in ('native','profile'):
    arms={m:sorted([r for r in samples if r['series']==series and r['mode']==m],key=lambda r:r['pair']) for m in ('scalar','batch')}
    keys=[k for k in arms['scalar'][0] if all(isinstance(r.get(k),(int,float)) for rows in arms.values() for r in rows)]
    metrics={}
    for k in keys:
        a=[r[k] for r in arms['scalar']]; b=[r[k] for r in arms['batch']]; gains=[x-y for x,y in zip(a,b)]
        rng=random.Random(110)
        boot=sorted(statistics.median(rng.choices(gains,k=len(gains))) for _ in range(20000))
        metrics[k]=dict(scalar_median=statistics.median(a),batch_median=statistics.median(b),
                        paired_batch_gain=statistics.median(gains),ci95=[boot[500],boot[19499]],
                        batch_wins=sum(g>0 for g in gains),pairs=len(gains))
    summary[series]=metrics
(out/'summary.json').write_text(json.dumps(summary,indent=2))
(out/'checks.json').write_text(json.dumps(checks,indent=2))
(out/'counts.json').write_text(json.dumps(counts,indent=2))
(out/'parity.json').write_text(json.dumps(dict(artifacts=gold,shm=shm),indent=2))
assert hashes=={p.name:sha(p) for p in (native,audit)}
assert all(sha(fixture/p)==h for p,h in manifest.items())
source_manifest=json.loads((out/'source-manifest.json').read_text())
assert all(sha(out/'source'/p)==h for p,h in source_manifest.items())
(out/'verified.json').write_text(json.dumps(dict(native_samples=30,profile_samples=14,
    input_unchanged=True,binaries_unchanged=True,sources_unchanged=True,artifact_parity=True,shm_parity=True),indent=2))
print(json.dumps({s:m['total_ms'] for s,m in summary.items()},indent=2),flush=True)
