"""Same-executable native PUBLISH A/B; independent cold semantic/SHM gates."""
import argparse,hashlib,json,os,random,shutil,statistics,subprocess
from pathlib import Path
repo=Path(__file__).resolve().parents[2];build=repo/'build/parallel-local-source-09-b1'
p=argparse.ArgumentParser();p.add_argument('output',type=Path);p.add_argument('--pairs',type=int,default=15);a=p.parse_args()
out=a.output.resolve();assert a.pairs>=5 and out.is_relative_to(repo/'build') and not out.exists();out.mkdir(parents=True)
shutil.copytree(repo/'build/parallel-local-source-08/publish-c-final/fixture',out/'fixture',ignore=shutil.ignore_patterns('.serverengine','__pycache__'));project=out/'fixture/project.json'
bins={}
for name in ('PublishReferenceStateB1','PublishReferenceAuditB1','SourceReferenceSemanticB1'):
 bins[name]=out/f'{name}.exe';shutil.copy2(build/'Release'/f'{name}.exe',bins[name])
def sha(path):
 with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
binary_hashes={name:sha(path) for name,path in bins.items()}
environment=dict(os.environ,PLS03_MODE='1',PLS04_EARLY='1',PLS06_DENSE='1',PLS07_OWN='1')
for key in list(environment):
 if key.startswith(('PLS03_','PLS04_','PLS06_','PLS07_','PLS08_','PLS09_')) and key not in ('PLS03_MODE','PLS04_EARLY','PLS06_DENSE','PLS07_OWN'):environment.pop(key)
environment.pop('CW_RUNTIME_SHM_AUDIT',None)
def invoke(exe,args,label,env):
 r=subprocess.run([str(exe),*map(str,args)],capture_output=True,text=True,env=env,timeout=180)
 (out/f'{label}.stdout').write_text(r.stdout);(out/f'{label}.stderr').write_text(r.stderr)
 assert r.returncode==0,(label,r.stdout,r.stderr)
 return r.stdout
def fields(text):
 result={}
 for line in text.splitlines():
  for item in line.split(','):
   if '=' in item:
    k,v=item.split('=',1)
    try:result[k]=float(v)
    except ValueError:pass
 return result

# Deep correctness runs retain fingerprints/origins and stress reverse task and
# physical insertion order. None of their diagnostic time is a benchmark sample.
semantic=[]
for indexed in (0,1):
 report=out/f'semantic-{indexed}.json'
 env=dict(environment,PLS09_INDEXED=str(indexed),PLS07_VERIFY='1',PLS08_REVERSE=str(indexed))
 invoke(bins['SourceReferenceSemanticB1'],[project,report]+(['--reverse'] if indexed else []),f'semantic-{indexed}',env)
 data=json.loads(report.read_text());assert data['semantic_fingerprint']=='13649286433448276567'
 assert data['counts']['linker_token_reads']==0
 semantic.append(data)
assert semantic[0]['semantic_fingerprint']==semantic[1]['semantic_fingerprint']
assert semantic[0]['canonical_numbering_fingerprint']==semantic[1]['canonical_numbering_fingerprint']
expected=out/'semantic-0.compiled.bin';artifacts=project.parent/'.serverengine'/project.name
gold={};audits={};rows=[]
def run(label,arm,*,cold=False,audit=False,profile=False):
 indexed=int(arm=='indexed');env=dict(environment,PLS09_INDEXED=str(indexed),PLS08_CANDIDATE=str(int(arm!='v2')))
 if cold:env.update(PLS08_OBJECT_AUDIT='1')
 if cold and arm!='v2':env.update(PLS08_EXPECTED_IMAGE=str(expected))
 if audit:env['PLS03_PROFILE_PATH']=str(out/f'{label}-{arm}.profile.json')
 exe=bins['PublishReferenceAuditB1' if audit else 'PublishReferenceStateB1']
 text=invoke(exe,['publish-timeline' if profile else 'publish',project,0],f'{label}-{arm}',env)
 data=fields(text)
 assert data['objects']==137584 and data['links']==367144 and data['pls09_indexed']==indexed
 digest={name:sha(artifacts/name) for name in ('compiled.bin','runtime.bin')}
 domain='v2' if arm=='v2' else 'final-state'
 if domain not in gold:gold[domain]=digest
 assert digest==gold[domain],(label,arm,'compiled/runtime digest mismatch')
 if cold:
  line=next(line for line in text.splitlines() if line.startswith('pls08_object_storage_sha256='))
  assert 'cb5aa2607c10f928b273ad8f5d33c29f3656190b3ec9597550cbf068a5f0d0e2' in line
 if audit:audits[arm]=json.loads((out/f'{label}-{arm}.profile.json').read_text())
 print(label,arm,round(data['total_ms'],2),round(data['pls08_alias_ms'],2),flush=True)
 return dict(label=label,arm=arm,**data)
for arm in ('legacy','indexed','v2'):run('cold',arm,cold=True)
for arm in ('legacy','indexed'):run('allocations',arm,cold=True,audit=True)
# Instrumented paths must preserve every logical request/lookup count. Only
# cache/visiting representation and its allocation traffic may differ.
assert audits['legacy']['path_reuse']==audits['indexed']['path_reuse']
for stage in audits['legacy']['stages']:
 assert audits['legacy']['stages'][stage]['counts']==audits['indexed']['stages'][stage]['counts'],stage
for control in ('legacy','v2'):
 for i in range(a.pairs):
  label=f'{control}-{i}'
  for arm in ((control,'indexed') if i%2==0 else ('indexed',control)):rows.append(run(label,arm))
  (out/'samples.json').write_text(json.dumps(rows,indent=2))
summary={}
for control in ('legacy','v2'):
 summary[control]={}
 pairs=[(next(r for r in rows if r['label']==v['label'] and r['arm']==control),v) for v in rows if v['arm']=='indexed' and v['label'].startswith(control+'-')]
 for metric in ('total_ms','pls08_alias_ms','pls08_full_with_teardown_ms','peak_ws_bytes'):
  gains=[c[metric]-v[metric] for c,v in pairs];rng=random.Random(9101)
  boot=sorted(statistics.median(rng.choices(gains,k=len(gains))) for _ in range(20000))
  summary[control][metric]=dict(control_median=statistics.median(c[metric] for c,v in pairs),indexed_median=statistics.median(v[metric] for c,v in pairs),paired_gain_median=statistics.median(gains),bootstrap95=[boot[500],boot[19499]],indexed_faster=sum(g>0 for g in gains),pairs=len(gains))
profiles={arm:run('profile',arm,profile=True) for arm in ('legacy','indexed','v2')}
assert {name:sha(path) for name,path in bins.items()}==binary_hashes
(out/'summary.json').write_text(json.dumps(dict(binary_hashes=binary_hashes,same_contract_digests=gold,semantic_fingerprint=semantic[0]['semantic_fingerprint'],canonical_numbering_fingerprint=semantic[0]['canonical_numbering_fingerprint'],comparisons=summary,allocation_audits=audits,diagnostic_profiles=profiles,production_changed=False),indent=2))
print(json.dumps(summary,indent=2))
