"""Predeclared independent 31-pair replication; retain/report the first series."""
import argparse,hashlib,json,os,random,shutil,statistics,subprocess
from pathlib import Path
repo=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('previous',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
previous=a.previous.resolve();out=a.output.resolve();assert out.is_relative_to(repo/'build') and not out.exists();out.mkdir(parents=True)
old=json.loads((previous/'summary.json').read_text());project=previous/'fixture/project.json'
exe=out/'PublishReferenceStateB1.exe';shutil.copy2(previous/exe.name,exe)
def sha(path):
 with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
assert sha(exe)==old['binary_hashes']['PublishReferenceStateB1']
env=dict(os.environ,PLS03_MODE='1',PLS04_EARLY='1',PLS06_DENSE='1',PLS07_OWN='1',PLS08_CANDIDATE='1')
for key in list(env):
 if key.startswith(('PLS03_','PLS04_','PLS06_','PLS07_','PLS08_','PLS09_')) and key not in ('PLS03_MODE','PLS04_EARLY','PLS06_DENSE','PLS07_OWN','PLS08_CANDIDATE'):env.pop(key)
env.pop('CW_RUNTIME_SHM_AUDIT',None);rows=[];artifacts=project.parent/'.serverengine'/project.name
for pair in range(31):
 for indexed in ((0,1) if pair%2==0 else (1,0)):
  r=subprocess.run([str(exe),'publish',str(project),'0'],capture_output=True,text=True,env=dict(env,PLS09_INDEXED=str(indexed)),timeout=180)
  (out/f'{pair}-{indexed}.stdout').write_text(r.stdout);(out/f'{pair}-{indexed}.stderr').write_text(r.stderr);assert r.returncode==0,r.stderr
  fields={}
  for line in r.stdout.splitlines():
   for item in line.split(','):
    if '=' in item:
     key,value=item.split('=',1)
     try:fields[key]=float(value)
     except ValueError:pass
  assert fields['pls09_indexed']==indexed and fields['objects']==137584 and fields['links']==367144
  assert {name:sha(artifacts/name) for name in ('compiled.bin','runtime.bin')}==old['same_contract_digests']['final-state']
  rows.append(dict(pair=pair,indexed=indexed,**fields));print(pair,indexed,fields['total_ms'],fields['pls08_alias_ms'],flush=True)
 (out/'samples.json').write_text(json.dumps(rows,indent=2))
result={}
for key in ('total_ms','pls08_alias_ms','pls08_full_with_teardown_ms','peak_ws_bytes'):
 control=[r[key] for r in rows if not r['indexed']];candidate=[r[key] for r in rows if r['indexed']]
 gains=[x-y for x,y in zip(control,candidate)];rng=random.Random(9131)
 boot=sorted(statistics.median(rng.choices(gains,k=31)) for _ in range(20000))
 result[key]=dict(control_median=statistics.median(control),indexed_median=statistics.median(candidate),paired_gain_median=statistics.median(gains),bootstrap95=[boot[500],boot[19499]],indexed_faster=sum(v>0 for v in gains),pairs=31)
assert sha(exe)==old['binary_hashes']['PublishReferenceStateB1']
(out/'summary.json').write_text(json.dumps(dict(previous=str(previous),binary_sha256=sha(exe),predeclared_pairs=31,comparisons=result),indent=2))
print(json.dumps(result,indent=2))
