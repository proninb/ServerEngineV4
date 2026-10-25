"""Existing cold-const encoding and fail-closed overlapping storage boundary."""
import json,os,subprocess,sys
from pathlib import Path
exe,root=map(lambda p:Path(p).resolve(),sys.argv[1:]);root.mkdir(parents=True,exist_ok=True)
def run(name,header,source,reverse=False,assign=False):
 d=root/name;d.mkdir(parents=True,exist_ok=True);(d/'types.h').write_text(header);(d/'source.cpp').write_text(source)
 entries=[dict(name='types',type='header',path='types.h'),dict(name='source',type='source',path='source.cpp')]
 if assign:
  (d/'input.txt').write_text('');entries.append(dict(name='input',type='assign',path='input.txt'))
 p=d/'project.json';p.write_text(json.dumps(dict(version=1,name=name,project=entries,preprocessor=dict(predefines=[]))))
 report=d/f'{int(reverse)}.json';env=dict(os.environ,PLS03_MODE='1',PLS04_EARLY='1',PLS06_DENSE='1',PLS07_OWN='1',PLS07_VERIFY='1',PLS08_REVERSE=str(int(reverse)))
 # Never let an old success artifact conceal a failed current invocation.
 report.with_suffix('.compiled.bin').unlink(missing_ok=True)
 r=subprocess.run([str(exe),str(p),str(report)]+(['--reverse'] if reverse else []),capture_output=True,text=True,env=env,timeout=30);return r,json.loads(report.read_text()),report
for reverse in (False,True):
 r,d,report=run('const-cold','struct Tag {};','const int n=0; const int m=-3; const double z=-0.0;',reverse)
 assert r.returncode==0,r.stderr;g=d['graph_writer'];assert g['cold_declarations']==3 and g['initializations']==0 and g['runtime_values']==3
 assert g['runtime_fields']['n']==0 and g['runtime_fields']['m']==(1<<32)-3 and g['runtime_fields']['z']==1<<63
 r,d,report=run('union-overlap','union U { int a; int b; };','U u; u.a=1; u.b=2;',reverse)
 assert r.returncode!=0 and 'Overlapping final Source storage ranges' in d.get('error',''),(r.stderr,d)
 assert not report.with_suffix('.compiled.bin').exists(),'Unverified overlapping artifact was written'
 for name,header,source,assign,error in (
  ('assign-input','struct I { int x; };','I i;',True,'Assign input is outside'),
  ('scalar-reference-write','struct R { int& ref; };','int x; R r; r.ref=x; r.ref=8;',False,'Reference binding needs endpoint')):
  r,d,report=run(name,header,source,reverse,assign)
  assert r.returncode!=0 and error in d.get('error',''),(name,r.stderr,d)
  assert not report.with_suffix('.compiled.bin').exists(),name
(root/'summary.json').write_text(json.dumps(dict(cold_const_runs=2,overlap_rejections=2,assign_rejections=2,scalar_reference_write_rejections=2,overlap_gap='Distinct canonical member paths can share physical union storage; no physical-order winner accepted'),indent=2))
print('PLS08: 2 cold const Runtime cases + 6 explicit unsupported-input rejections passed')
