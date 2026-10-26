"""Cold counters/allocation census, never used as performance samples."""
import hashlib,json,os,subprocess,sys
from pathlib import Path
repo=Path(__file__).resolve().parents[2];out=Path(sys.argv[1]).resolve()
assert out.is_relative_to(repo/'build');frozen=json.loads((out/'summary.json').read_text());exe=repo/'build/parallel-local-source-10-c9/Release/SourceAudit10C9.exe'
env={k:v for k,v in os.environ.items() if not k.startswith(('PLS','CW_RUNTIME_SHM_AUDIT'))};results=[]
for arm in ('v2','c7'):
 e=dict(env,PLS08_CANDIDATE=str(int(arm=='c7')),PLS10_C7_FAST='1',PLS10_WORKERS='4',PLS10_C9_PROFILE='1',PLS08_OBJECT_AUDIT='1')
 p=subprocess.run([str(exe),'publish-timeline',str(out/'fixture/project.json'),'0'],env=e,capture_output=True,text=True,timeout=180)
 (out/f'audit-{arm}.stdout').write_text(p.stdout);(out/f'audit-{arm}.stderr').write_text(p.stderr);assert p.returncode==0,p.stderr[-2000:]
 assert 'pls08_object_storage_sha256=cb5aa2607c10f928b273ad8f5d33c29f3656190b3ec9597550cbf068a5f0d0e2' in p.stdout
 for name,digest in frozen['artifact_hashes'][arm].items():
  with (out/'fixture/.serverengine/project.json'/name).open('rb') as f:assert hashlib.file_digest(f,'sha256').hexdigest()==digest,(arm,name)
 fields={}
 for line in p.stdout.splitlines():
  for item in line.split(','):
   if '=' in item:
    k,v=item.split('=',1)
    if k.startswith('c9_'):
     try:fields[k]=float(v)
     except ValueError:pass
 results.append(dict(arm=arm,**fields));print(arm,json.dumps({k:v for k,v in fields.items() if k.startswith('c9_audit_')}),flush=True)
 assert fields['c9_audit_allocations']==sum(fields[f'c9_audit_group{g}_calls'] for g in (1,2,3))
 assert fields['c9_audit_requested']==sum(fields[f'c9_audit_group{g}_requested'] for g in (1,2,3))
with exe.open('rb') as f:binary=hashlib.file_digest(f,'sha256').hexdigest()
(out/'audit.json').write_text(json.dumps(dict(binary_hash=binary,results=results),indent=2))
