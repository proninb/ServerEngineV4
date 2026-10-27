"""One native same-executable candidate A/B, after sampler and tests finish."""
from pathlib import Path
import hashlib,json,os,random,shutil,statistics,subprocess,sys
repo=Path(__file__).resolve().parents[2];root=Path(sys.argv[1]).resolve()
out=root/'candidate-results';out.mkdir(exist_ok=False)
logs=out/'logs';logs.mkdir()
exe=out/'SourceCriticalCandidate.exe'
shutil.copy2(root/'candidate-build/Release/SourceCriticalCandidate.exe',exe)
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
binary=sha(exe); (out/'binary.json').write_text(json.dumps(dict(sha256=binary,execution='unsandboxed medium-integrity',
    base='same optimized production .obj except private new-link Graph lookup path and benchmark post-timer audit'),indent=2))
project=repo/'build/parallel-local-source-10-c9/ab-01/fixture/project.json'
artifact=project.parent/'.serverengine/project.json'
inputs=json.loads((root/'capture/input-manifest.json').read_text())
assert all(sha(project.parent/p)==h for p,h in inputs.items())
gold=json.loads((root/'capture/manifest.json').read_text())['golden_hashes']
backup=out/'original-artifacts';backup.mkdir()
for name in gold:shutil.copy2(artifact/name,backup/name)
base={k:v for k,v in os.environ.items() if not k.startswith(('CW_','PLS','FAST_ACQUIRE_','PUBLISH_PIPELINE_','SOURCE_STRING_BATCH_','SOURCE_CRITICAL_'))}
samples=[];checks=[];shm=None
def run(mode,operation,tag,audit=False):
    global shm
    env=dict(base,SOURCE_CRITICAL_LINK_MODE=mode)
    if audit:env['SOURCE_CRITICAL_SHM_AUDIT']='1'
    command=[str(exe),operation,str(project),'24027']
    p=subprocess.run(command,env=env,cwd=repo,capture_output=True,text=True,timeout=180)
    label=f'{tag}-{mode}-{operation}'
    (logs/(label+'.stdout')).write_text(p.stdout);(logs/(label+'.stderr')).write_text(p.stderr)
    assert p.returncode==0,(label,p.stderr)
    row={}
    for line in p.stdout.splitlines():
        for part in line.split(','):
            if '=' not in part:continue
            k,v=part.split('=',1)
            try:row[k]=float(v)
            except ValueError:row[k]=v
    assert (row['types'],row['objects'],row['links'])==(24027,137584,367144)
    assert all(sha(artifact/n)==h for n,h in gold.items()),label
    if audit:
        if shm is None:shm=row['shm_audit_sha256']
        assert shm==row['shm_audit_sha256']=='61334dee4dd897389b17658ff54092df60d62c54024856673b4567db60ed9711'
    row.update(mode=mode,operation=operation,tag=tag,command=command)
    print(label,row['total_ms'],'ms',flush=True)
    return row
try:
    for mode in ('baseline','candidate'):
        for operation in ('publish','audit','load'):checks.append(run(mode,operation,'pre',True))
    for mode in ('candidate','baseline'):checks.append(run(mode,'publish','warm'))
    for series,operation,n in (('native','publish',15),('profile','publish-timeline',7)):
        for pair in range(n):
            for mode in (('baseline','candidate') if pair%2==0 else ('candidate','baseline')):
                row=run(mode,operation,f'{series}-{pair:02}');row.update(series=series,pair=pair);samples.append(row)
            (out/'samples.json').write_text(json.dumps(samples,indent=2))
    for mode in ('candidate','baseline'):
        checks.append(run(mode,'audit','post',True));checks.append(run(mode,'load','post',True))
finally:
    for name in gold:shutil.copy2(backup/name,artifact/name)
summary={}
for series in ('native','profile'):
    arms={m:sorted([r for r in samples if r['series']==series and r['mode']==m],key=lambda r:r['pair']) for m in ('baseline','candidate')}
    metrics={}
    for key in ('total_ms','source_ms','peak_ws_bytes'):
        if key not in arms['baseline'][0]:continue
        a=[r[key] for r in arms['baseline']];b=[r[key] for r in arms['candidate']];g=[x-y for x,y in zip(a,b)]
        rng=random.Random(110);boot=sorted(statistics.median(rng.choices(g,k=len(g))) for _ in range(20000))
        metrics[key]=dict(baseline_median=statistics.median(a),candidate_median=statistics.median(b),paired_gain=statistics.median(g),
                          ci95=[boot[500],boot[19499]],wins=sum(x>0 for x in g),pairs=len(g))
    summary[series]=metrics
(out/'summary.json').write_text(json.dumps(summary,indent=2));(out/'checks.json').write_text(json.dumps(checks,indent=2))
assert sha(exe)==binary and all(sha(project.parent/p)==h for p,h in inputs.items())
assert all(sha(root/'candidate-source'/p)==h for p,h in json.loads((root/'candidate-source-manifest.json').read_text()).items())
(out/'verified.json').write_text(json.dumps(dict(native_samples=30,profile_samples=14,inputs_unchanged=True,
    binary_unchanged=True,candidate_sources_unchanged=True,artifact_and_shm_parity=True,original_artifacts_restored=True),indent=2))
print(json.dumps(summary,indent=2),flush=True)
