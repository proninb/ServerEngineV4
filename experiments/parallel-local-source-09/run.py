"""Coarse clocks and detailed counters are separate; no optimization claim."""
import argparse,hashlib,json,os,random,shutil,statistics,subprocess
from pathlib import Path
repo=Path(__file__).resolve().parents[2];build=repo/'build/parallel-local-source-09'
p=argparse.ArgumentParser();p.add_argument('output',type=Path);p.add_argument('--pairs',type=int,default=15);p.add_argument('--detail-pairs',type=int,default=5);a=p.parse_args()
out=a.output.resolve();assert out.is_relative_to(repo/'build') and not out.exists();out.mkdir(parents=True)
source=repo/'build/parallel-local-source-08/publish-c-final/fixture'
shutil.copytree(source,out/'fixture',ignore=shutil.ignore_patterns('.serverengine','__pycache__'));project=out/'fixture/project.json'
executables={}
for arm,path in dict(baseline=build/'base/Release/PublishFinalState08.exe',phases=build/'Release/PublishAliasPhases09.exe',details=build/'Release/PublishAliasDetails09.exe').items():
 executables[arm]=out/path.name;shutil.copy2(path,executables[arm])
def digest(path):
 with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
binary_hashes={arm:digest(path) for arm,path in executables.items()}
env=dict(os.environ,PLS08_CANDIDATE='1',PLS03_MODE='1',PLS04_EARLY='1',PLS06_DENSE='1')
for key in list(env):
 if key.startswith(('PLS03_','PLS04_','PLS06_','PLS07_','PLS08_')) and key not in ('PLS08_CANDIDATE','PLS03_MODE','PLS04_EARLY','PLS06_DENSE'):env.pop(key)
env.pop('CW_RUNTIME_SHM_AUDIT',None)
artifacts=project.parent/'.serverengine'/project.name;gold=None;rows=[];profiles={}
expected=repo/'build/parallel-local-source-08/publish-c-final/oracle.compiled.bin'
def run(label,arm,cold=False):
 global gold
 environment=dict(env);profile=out/f'{label}-{arm}.profile.json'
 if arm!='baseline':environment['PLS03_PROFILE_PATH']=str(profile)
 if cold:environment.update(PLS08_EXPECTED_IMAGE=str(expected),PLS08_OBJECT_AUDIT='1')
 r=subprocess.run([str(executables[arm]),'publish',str(project),'0'],capture_output=True,text=True,env=environment,timeout=180)
 (out/f'{label}-{arm}.stdout').write_text(r.stdout);(out/f'{label}-{arm}.stderr').write_text(r.stderr)
 assert r.returncode==0,(label,arm,r.stdout,r.stderr)
 data={}
 for line in r.stdout.splitlines():
  for item in line.split(','):
   if '=' in item:
    key,value=item.split('=',1)
    try:data[key]=float(value)
    except ValueError:pass
 assert data['objects']==137584 and data['links']==367144 and data['pls08_candidate']==1
 hashed={name:digest(artifacts/name) for name in ('compiled.bin','runtime.bin')}
 if gold is None:gold=hashed
 assert hashed==gold,(label,arm,'semantic/persistence byte parity failed')
 if cold:
  audit=next(line for line in r.stdout.splitlines() if line.startswith('pls08_object_storage_sha256='))
  assert 'cb5aa2607c10f928b273ad8f5d33c29f3656190b3ec9597550cbf068a5f0d0e2' in audit
  assert 'pls08_expected_values=705238,pls08_expected_links=367144' in r.stdout
 if profile.exists():profiles[f'{label}-{arm}']=json.loads(profile.read_text())
 print(label,arm,round(data['total_ms'],2),round(data['pls08_alias_ms'],2),flush=True)
 return dict(label=label,arm=arm,**data)

# Each observer must pass actual resident values/pointers/defaults parity first.
for arm in executables:run('cold',arm,True)
for observer,n in (('phases',a.pairs),('details',a.detail_pairs)):
 for i in range(n):
  label=f'{observer}-{i}'
  for arm in (('baseline',observer) if i%2==0 else (observer,'baseline')):rows.append(run(label,arm))
  (out/'samples.json').write_text(json.dumps(rows,indent=2))

summary={}
for observer in ('phases','details'):
 paired=[(next(r for r in rows if r['label']==o['label'] and r['arm']=='baseline'),o) for o in rows if o['arm']==observer]
 summary[observer]={}
 for metric in ('total_ms','pls08_alias_ms'):
  delta=[o[metric]-b[metric] for b,o in paired];rng=random.Random(909)
  bootstrap=sorted(statistics.median(rng.choices(delta,k=len(delta))) for _ in range(10000))
  summary[observer][metric]=dict(baseline_median=statistics.median(b[metric] for b,o in paired),observer_median=statistics.median(o[metric] for b,o in paired),paired_overhead_median=statistics.median(delta),bootstrap95=[bootstrap[250],bootstrap[9749]],pairs=len(delta))
phase_data=[v for k,v in profiles.items() if k.startswith('phases-') and k.endswith('-phases')]
stages={key:statistics.median(p['stages'][key]['ms'] for p in phase_data) for key in phase_data[0]['stages']}
details=[v for k,v in profiles.items() if k.startswith('details-') and k.endswith('-details')]
for record in details[1:]:
 assert record['path_reuse']==details[0]['path_reuse']
 for stage in record['stages']:assert record['stages'][stage]['counts']==details[0]['stages'][stage]['counts']
assert {arm:digest(path) for arm,path in executables.items()}==binary_hashes
(out/'summary.json').write_text(json.dumps(dict(algorithm_changed=False,binary_hashes=binary_hashes,artifact_digests=gold,observer_overhead=summary,coarse_stage_medians_ms=stages,detail_counts=details[0],detail_stage_times_are_not_production_costs=True),indent=2))
print(json.dumps(dict(observer_overhead=summary,coarse_stages=stages),indent=2))
