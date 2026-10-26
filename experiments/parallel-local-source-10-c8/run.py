"""One local Graph experiment, frozen uninstrumented native controls."""
import hashlib,json,os,random,shutil,statistics,subprocess,sys
from pathlib import Path
repo=Path(__file__).resolve().parents[2];out=Path(sys.argv[1]).resolve()
assert out.is_relative_to(repo/'build') and not out.exists();out.mkdir(parents=True)
shutil.copytree(repo/'build/parallel-local-source-08/publish-c-final/fixture',out/'fixture',ignore=shutil.ignore_patterns('.serverengine','__pycache__'))
bins={}
for arm,experiment,name in [('c7','parallel-local-source-10-c7','PublishTypedView10C7'),('native','parallel-local-source-10-c8','PublishSingleLookup10C8'),('audit','parallel-local-source-10-c8','SingleLookupAudit10C8')]:
 bins[arm]=out/(name+'.exe');shutil.copy2(repo/'build'/experiment/'Release'/(name+'.exe'),bins[arm])
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
binary={k:sha(p) for k,p in bins.items()};rows=[];gold={};cold={};audits={}
environment={k:v for k,v in os.environ.items() if not k.startswith(('PLS','CW_RUNTIME_SHM_AUDIT'))}
def run(label,arm,verify=False):
 single=arm in ('single','audit-single');env=dict(environment,PLS08_CANDIDATE=str(int(arm!='v2')),PLS10_WORKERS='4',PLS10_C7_FAST='1',PLS10_C8_SINGLE=str(int(single)))
 if verify:env['PLS08_OBJECT_AUDIT']='1'
 key='audit' if arm.startswith('audit') else 'c7' if arm in ('c7','v2') else 'native'
 r=subprocess.run([str(bins[key]),'publish',str(out/'fixture/project.json'),'0'],env=env,capture_output=True,text=True,timeout=180)
 (out/f'{label}-{arm}.stdout').write_text(r.stdout);(out/f'{label}-{arm}.stderr').write_text(r.stderr);assert r.returncode==0,(label,arm,r.stderr[-2500:])
 fields={}
 for line in r.stdout.splitlines():
  for item in line.split(','):
   if '=' in item:
    k,v=item.split('=',1)
    try:fields[k]=float(v)
    except ValueError:pass
 artifacts=out/'fixture/.serverengine/project.json';domain='v2' if arm=='v2' else 'c7';digest={f:sha(artifacts/f) for f in ('compiled.bin','runtime.bin')}
 if domain not in gold:gold[domain]=digest
 assert gold[domain]==digest,(label,arm,'artifact mismatch')
 if verify:
  cold[arm]=next(s for s in r.stdout.splitlines() if s.startswith('pls08_object_storage_sha256='))
  assert 'cb5aa2607c10f928b273ad8f5d33c29f3656190b3ec9597550cbf068a5f0d0e2' in cold[arm]
 if arm.startswith('audit'):
  audits[arm]=fields;assert fields['c8_new_bindings']==367144 and fields['c8_replacements']==0
  assert fields['c8_lookups']==(399960 if single else 767104)
  assert fields['c8_hits']==32816 and fields['c8_misses']==(367144 if single else 734288)
  assert fields['c8_known_absent']==(367144 if single else 0)
 elif key=='native':assert fields['c8_lookups']==0 and fields['c8_new_bindings']==0,'native counters not compiled out'
 print(label,arm,round(fields['total_ms'],2),round(fields['pls08_linking_ms'],2),flush=True)
 return dict(label=label,arm=arm,**fields)
for arm in ('c7','off','single','audit-off','audit-single','v2'):run('cold',arm,True)
assert len(set(cold.values()))==1
arms=['c7','off','single','v2']
for i in range(15):
 order=arms[i%4:]+arms[:i%4]
 if i%2:order=order[::-1]
 for arm in order:rows.append(run(str(i),arm))
 (out/'samples.json').write_text(json.dumps(rows,indent=2))
summary={}
for control,candidate in [('c7','off'),('off','single'),('c7','single'),('v2','single')]:
 summary[control+'->'+candidate]={}
 for metric in ('total_ms','pls08_linking_ms','pls08_full_with_teardown_ms','peak_ws_bytes'):
  if control=='v2' and metric=='pls08_linking_ms':continue
  pairs=[(next(r[metric] for r in rows if r['label']==str(i) and r['arm']==control),next(r[metric] for r in rows if r['label']==str(i) and r['arm']==candidate)) for i in range(15)]
  gains=[a-b for a,b in pairs];rng=random.Random(108);boot=sorted(statistics.median(rng.choices(gains,k=15)) for _ in range(20000));unit='bytes' if metric=='peak_ws_bytes' else 'ms'
  summary[control+'->'+candidate][metric]=dict(unit=unit,control_median=statistics.median(a for a,b in pairs),candidate_median=statistics.median(b for a,b in pairs),paired_gain=statistics.median(gains),bootstrap95=[boot[500],boot[19499]],candidate_wins=sum(g>0 for g in gains))
assert binary=={k:sha(p) for k,p in bins.items()}
(out/'summary.json').write_text(json.dumps(dict(binary_hashes=binary,cold=cold,digests=gold,audits=audits,pairs=15,comparisons=summary),indent=2));print(json.dumps(summary,indent=2))
