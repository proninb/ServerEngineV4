"""Frozen C10 versus historical V2; caller must execute outside sandbox."""
from pathlib import Path
import hashlib, json, os, platform, random, shutil, statistics, subprocess, sys

repo = Path(__file__).resolve().parents[2]
out = Path(sys.argv[1]).resolve()
assert out.is_relative_to(repo/'build') and not out.exists()
out.mkdir(parents=True)
logs, bins = out/'logs', out/'binaries'
logs.mkdir()
bins.mkdir()
original = repo/'build/parallel-local-source-10-c10/ab-01'
historic = json.loads((original/'summary.json').read_text())
fixture = original/'fixture'
artifact = fixture/'.serverengine/project.json'
project = fixture/'project.json'
arms = dict(v2=original/'c7.exe', c10=original/'native.exe')

def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()

hashes = {a:sha(p) for a,p in arms.items()}
assert hashes['v2']==historic['binary_hashes']['c7']
assert hashes['c10']==historic['binary_hashes']['native']
for a,p in arms.items():
    shutil.copy2(p,bins/(a+'.exe'))
inputs = {p.relative_to(fixture).as_posix():sha(p) for p in sorted(fixture.rglob('*'))
          if p.is_file() and '.serverengine' not in p.relative_to(fixture).parts}
(out/'input-manifest.json').write_text(json.dumps(inputs,indent=2))
(out/'metadata.json').write_text(json.dumps(dict(
    execution='unsandboxed require_escalated caller; recorded tool invocation',
    note='Launch label does not itself change sandbox mode.',os=platform.platform(),
    logical_cpus=os.cpu_count(),project=str(project),binaries=hashes,
    timer='native internal total_ms before resident teardown',
    parser_workers=4,ordered_window=8,profiles=False),indent=2))
base = {k:v for k,v in os.environ.items()
        if not k.startswith(('CW_', 'PLS', 'FAST_ACQUIRE_', 'PUBLISH_PIPELINE_'))}
backup = out/'original-artifacts'
backup.mkdir()
names = ('compiled.bin','runtime.bin')
for name in names:
    shutil.copy2(artifact/name,backup/name)
samples, checks, digests = [], [], {}

def run(arm,tag,audit=False):
    env = dict(base,PLS08_CANDIDATE='0' if arm=='v2' else '1',
               PLS10_C7_FAST='1',PLS10_WORKERS='4',PLS10_C10_REUSE='1')
    if audit:
        env['PLS08_OBJECT_AUDIT']='1'
    command = [str(arms[arm]),'publish',str(project),'0']
    result = subprocess.run(command,cwd=repo,env=env,capture_output=True,text=True,timeout=180)
    ident = tag+'-'+arm
    (logs/(ident+'.stdout')).write_text(result.stdout)
    (logs/(ident+'.stderr')).write_text(result.stderr)
    assert result.returncode==0,(ident,result.stderr)
    row = {}
    for line in result.stdout.splitlines():
        for part in line.split(','):
            if '=' not in part:
                continue
            k,v = part.split('=',1)
            try:
                row[k]=float(v)
            except ValueError:
                row[k]=v
    assert (row['types'],row['objects'],row['links'])==(24027,137584,367144)
    assert row['pls08_candidate']==int(arm=='c10')
    assert row['c7_fast_calls']==row['c7_verified']==0
    if arm=='c10':
        assert row['pls10c10_reuse']==1 and row['ordered_workers']==4 and row['ordered_window']==8
    payload = {n:sha(artifact/n) for n in names}
    assert payload==historic['artifact_hashes']['v2' if arm=='v2' else 'c7'],(ident,payload)
    if audit:
        digest = row['pls08_object_storage_sha256']
        assert digest=='cb5aa2607c10f928b273ad8f5d33c29f3656190b3ec9597550cbf068a5f0d0e2'
        digests[ident]=digest
    row.update(arm=arm,tag=tag,command=command,artifact_sha256=payload)
    print(ident,row['total_ms'],'ms',flush=True)
    return row

try:
    for arm in ('v2','c10'):
        checks.append(run(arm,'pre',True))
    for arm in ('c10','v2'):
        checks.append(run(arm,'warm'))
    for pair in range(15):
        for arm in (('v2','c10') if pair%2==0 else ('c10','v2')):
            row=run(arm,f'native-{pair:02}')
            row['pair']=pair
            samples.append(row)
        (out/'samples.json').write_text(json.dumps(samples,indent=2))
    for arm in ('c10','v2'):
        checks.append(run(arm,'post',True))
finally:
    for name in names:
        shutil.copy2(backup/name,artifact/name)

values = {a:sorted([r for r in samples if r['arm']==a],key=lambda r:r['pair']) for a in arms}
summary = {}
for metric in ('total_ms','pls08_full_with_teardown_ms','peak_ws_bytes'):
    a=[r[metric] for r in values['v2']]
    b=[r[metric] for r in values['c10']]
    gains=[x-y for x,y in zip(a,b)]
    rng=random.Random(110)
    boot=sorted(statistics.median(rng.choices(gains,k=15)) for _ in range(20000))
    summary[metric]=dict(v2_median=statistics.median(a),c10_median=statistics.median(b),
                         paired_c10_gain=statistics.median(gains),ci95=[boot[500],boot[19499]],
                         c10_wins=sum(g>0 for g in gains),pairs=15)
(out/'summary.json').write_text(json.dumps(summary,indent=2))
(out/'checks.json').write_text(json.dumps(checks,indent=2))
assert all(sha(fixture/p)==h for p,h in inputs.items())
assert hashes=={a:sha(p) for a,p in arms.items()}
assert all(sha(artifact/n)==sha(backup/n) for n in names)
(out/'verified.json').write_text(json.dumps(dict(inputs_unchanged=True,binaries_unchanged=True,
    original_artifacts_restored=True,artifact_gold_per_arm=True,object_storage_digests=digests,
    native_samples=len(samples),additional_checks=len(checks)),indent=2))
print(json.dumps(summary,indent=2),flush=True)
