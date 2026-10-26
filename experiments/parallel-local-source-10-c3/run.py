"""Frozen native PUBLISH comparison; cold audits/hashing stay outside timer."""
import hashlib,json,os,random,shutil,statistics,subprocess,sys
from pathlib import Path
repo=Path(__file__).resolve().parents[2];out=Path(sys.argv[1]).resolve()
assert out.is_relative_to(repo/'build') and not out.exists();out.mkdir(parents=True)
shutil.copytree(repo/'build/parallel-local-source-08/publish-c-final/fixture',out/'fixture',ignore=shutil.ignore_patterns('.serverengine','__pycache__'))
adapted='--explicit-globals' in sys.argv[2:];patched=[]
if adapted:
 def visit(project):
  data=json.loads(project.read_text())
  def items(entries):
   for entry in entries:
    if entry['type']=='group':items(entry.get('children',[]))
    elif entry['type']=='project':visit(project.parent/entry['path'])
    elif entry['type']=='source':
     source=project.parent/entry['path'];text=source.read_text(encoding='utf-8-sig')
     if source.name!='globals.source':
      source.write_text('extern bool S[123][52]; extern long SW[641][52];\n'+text,encoding='utf-8');patched.append(str(source.relative_to(out/'fixture')))
  items(data['project'])
 visit(out/'fixture/project.json')
 (out/'input-adaptation.json').write_text(json.dumps(dict(reason='Explicit file-local declarations required by the user contract',prepended='extern bool S[123][52]; extern long SW[641][52];',files=patched),indent=2))
exe=out/'PublishOrderedSource10C3.exe';shutil.copy2(repo/'build/parallel-local-source-10-c3/Release'/exe.name,exe)
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
binary=sha(exe);rows=[];digests={};cold={}
environment={k:v for k,v in os.environ.items() if not k.startswith(('PLS','CW_RUNTIME_SHM_AUDIT'))}
def run(label,workers,control=False,audit=False):
 env=dict(environment,PLS08_CANDIDATE=str(int(not control)),PLS10_WORKERS=str(workers))
 if audit:env['PLS08_OBJECT_AUDIT']='1'
 r=subprocess.run([str(exe),'publish',str(out/'fixture/project.json'),'0'],capture_output=True,text=True,env=env,timeout=180)
 (out/f'{label}.stdout').write_text(r.stdout);(out/f'{label}.stderr').write_text(r.stderr)
 assert r.returncode==0,(label,r.stdout,r.stderr)
 fields={}
 for line in r.stdout.splitlines():
  for part in line.split(','):
   if '=' in part:
    k,v=part.split('=',1)
    try:fields[k]=float(v)
    except ValueError:pass
 artifacts=out/'fixture/.serverengine/project.json';d={name:sha(artifacts/name) for name in ('compiled.bin','runtime.bin')};domain='v2' if control else 'ordered'
 if domain in digests:assert digests[domain]==d,(label,'nondeterministic artifacts')
 else:digests[domain]=d
 if audit:cold[label]=next(s for s in r.stdout.splitlines() if s.startswith('pls08_object_storage_sha256='))
 print(label,round(fields['total_ms'],2),round(fields['pls08_linking_ms'],2),flush=True)
 return dict(label=label,workers=workers,control=control,**fields)
for label,w,control in [('cold-v2',1,True),('cold-1',1,False),('cold-4',4,False)]:run(label,w,control,True)
assert len(set(cold.values()))==1,('UnitProXL storage differs',cold)
for i in range(15):
 for control in ((True,False) if i%2==0 else (False,True)):
  rows.append(run(f'{i}-'+('v2' if control else 'ordered'),4,control))
 (out/'samples.json').write_text(json.dumps(rows,indent=2))
gains=[rows[i]['total_ms']-rows[i+1]['total_ms'] if rows[i]['control'] else rows[i+1]['total_ms']-rows[i]['total_ms'] for i in range(0,len(rows),2)]
rng=random.Random(103);boot=sorted(statistics.median(rng.choices(gains,k=len(gains))) for _ in range(20000))
summary=dict(binary_sha256=binary,input_adapted=adapted,patched_files=len(patched),cold=cold,digests=digests,pairs=15,v2_median=statistics.median(r['total_ms'] for r in rows if r['control']),ordered_median=statistics.median(r['total_ms'] for r in rows if not r['control']),paired_gain_ms=statistics.median(gains),bootstrap95=[boot[500],boot[19499]],ordered_wins=sum(g>0 for g in gains))
assert binary==sha(exe);(out/'summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2))
