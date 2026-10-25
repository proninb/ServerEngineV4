"""PLS08-C private full PUBLISH A/B, with native resident oracle before timing."""
import argparse,csv,hashlib,json,os,random,shutil,statistics,subprocess,time
from pathlib import Path
repo=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('output',type=Path);p.add_argument('--project',type=Path,default=repo/'build/publish-subsecond-01/fixture/project.json');p.add_argument('--pairs',type=int,default=15)
a=p.parse_args();out=a.output.resolve();project=a.project.resolve()
assert a.pairs>=3 and out.is_relative_to(repo/'build') and not out.exists(),'Use a NEW output directory under workspace/build'
out.mkdir(parents=True);fixture=out/'fixture'
shutil.copytree(project.parent,fixture,ignore=shutil.ignore_patterns('.serverengine','__pycache__','*.etl'))
project=fixture/project.name;built=repo/'build/parallel-local-source-08/Release/PublishFinalState08.exe';oracle_exe=built.with_name('PublishGraphWriter08.exe')
exe=out/'PublishFinalState08.exe';shutil.copy2(built,exe)
def sha256(path):
 with path.open('rb') as file:return hashlib.file_digest(file,'sha256').hexdigest()
binary_sha256=sha256(exe)
env=dict(os.environ,PLS03_MODE='1',PLS04_EARLY='1',PLS06_DENSE='1',PLS07_OWN='1')
for key in ('PLS08_COLD_CHECK','PLS08_EXPECTED_IMAGE','PLS08_OBJECT_AUDIT','PLS03_AUDIT_PATH','PLS03_PHASES_PATH','PLS04_MEMORY_PATH','PLS06_HEADS_PATH','PLS07_OWNER_PATH','CW_RUNTIME_SHM_AUDIT'):
 env.pop(key,None)

def invoke(command,name,environment):
 start=time.perf_counter();r=subprocess.run(command,capture_output=True,text=True,env=environment,timeout=180)
 (out/f'{name}.stdout').write_text(r.stdout);(out/f'{name}.stderr').write_text(r.stderr)
 assert r.returncode==0,(name,r.stdout,r.stderr)
 return r.stdout,(time.perf_counter()-start)*1000

# Standalone A/B oracle and native resident validation are separate processes,
# never included in the timed PUBLISH interval or the paired sample population.
oracle=out/'oracle.json'
invoke([str(oracle_exe),str(project),str(oracle)],'preflight',dict(env,PLS07_VERIFY='1',PLS08_REVERSE='0'))
expected=json.loads(oracle.read_text());image=oracle.with_suffix('.compiled.bin')
stdout,_=invoke([str(exe),'publish',str(project),'0'],'resident-parity',dict(env,PLS08_CANDIDATE='1',PLS08_EXPECTED_IMAGE=str(image),PLS08_OBJECT_AUDIT='1'))
assert f"pls08_expected_values={expected['graph_writer']['initializations']},pls08_expected_links={expected['graph_writer']['links']}" in stdout
control_stdout,_=invoke([str(exe),'publish',str(project),'0'],'control-object-audit',dict(env,PLS08_CANDIDATE='0',PLS08_OBJECT_AUDIT='1'))
audit=lambda text:next(line for line in text.splitlines() if line.startswith('pls08_object_storage_sha256='))
assert audit(stdout)==audit(control_stdout),'Object storage/defaults differ; corpus is not a native Runtime parity domain for full PUBLISH A/B'
object_audit=audit(stdout)

def fields(stdout):
 result={}
 for line in stdout.splitlines():
  for item in line.split(','):
   if '=' in item:
    k,v=item.split('=',1)
    try:result[k]=float(v)
    except ValueError:pass
 return result

artifacts=fixture/'.serverengine'/project.name
digests={};rows=[]
def run(pair,arm,mode='publish',warmup=False):
 stdout,wall=invoke([str(exe),mode,str(project),'0'],f'{pair}-{arm}-{mode}',dict(env,PLS08_CANDIDATE=str(int(arm=='candidate'))))
 data=fields(stdout)
 assert data['objects']==expected['graph_writer']['objects'] and data['links']==expected['graph_writer']['links']
 assert data['pls08_candidate']==int(arm=='candidate') and data['pls08_full_with_teardown_ms']>=data['total_ms']
 assert not any((artifacts/f).exists() for f in ('source.bin','database.bin','project.manifest'))
 # Hash only AFTER the child PUBLISH and teardown timers have finished. New
 # contract has intentional old-byte differences, so each arm has its own gold.
 digest={f:sha256(artifacts/f) for f in ('compiled.bin','runtime.bin')}
 if arm not in digests:digests[arm]=digest
 assert digest==digests[arm],(pair,arm,'nondeterministic persisted image')
 row=dict(pair=pair,arm=arm,mode=mode,process_ms=wall,**data)
 if not warmup:rows.append(row)
 print(pair,arm,round(data['total_ms'],2),round(data['pls08_full_with_teardown_ms'],2),flush=True)
 return row

for arm in ('control','candidate'):run('warmup',arm,warmup=True)
for pair in range(a.pairs):
 for arm in (('control','candidate') if pair%2==0 else ('candidate','control')):run(pair,arm)
 with (out/'samples.json').open('w') as f:json.dump(rows,f,indent=2)

def summary(metric):
 control=[r[metric] for r in rows if r['arm']=='control'];candidate=[r[metric] for r in rows if r['arm']=='candidate']
 differences=[x-y for x,y in zip(control,candidate)];rng=random.Random(824)
 boot=sorted(statistics.median(rng.choices(differences,k=len(differences))) for _ in range(20000))
 return dict(control_median=statistics.median(control),candidate_median=statistics.median(candidate),paired_gain_median=statistics.median(differences),paired_gain_bootstrap95=[boot[500],boot[19499]],candidate_faster=sum(d>0 for d in differences),pairs=len(differences))
results={key:summary(key) for key in ('total_ms','pls08_full_with_teardown_ms','peak_ws_bytes')}
# Phase-only telemetry is collected separately: no per-token clocks in A/B.
profiles=[run('profile',arm,mode='publish-timeline',warmup=True) for arm in ('control','candidate')]
assert sha256(exe)==binary_sha256,'Executable changed during the paired series'
(out/'summary.json').write_text(json.dumps(dict(full_publish_benchmark=True,production_changed=False,semantic_byte_parity_required=False,binary_sha256=binary_sha256,object_storage_audit=object_audit,correctness_preflight=expected['graph_writer'],metrics=results,digests=digests,phase_profiles=profiles),indent=2))
with (out/'samples.csv').open('w',newline='') as f:
 writer=csv.DictWriter(f,fieldnames=sorted({key for row in rows for key in row}));writer.writeheader();writer.writerows(rows)
print(json.dumps(results,indent=2))
