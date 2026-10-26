"""Exercise caught unresolved rounds, recursive growth and unchanged first errors."""
import json,os,subprocess,sys
from pathlib import Path
exe,root=map(lambda p:Path(p).resolve(),sys.argv[1:]);root.mkdir(parents=True,exist_ok=True)
header='struct I { int x=5; }; struct R { I& ref; }; struct W { R& link; };'
def chain(n):
 return 'I b;'+''.join(f'I& p{i};' for i in range(n))+''.join(f'p{i}='+('b' if i==n-1 else f'p{i+1}')+';' for i in range(n))+'p0.x=9; b.x=13;'
cases={
 'retry-context':('I b; R r; W w; W& u; u=w; w.link.ref=b; u.link=r; w.link.ref.x=9; b.x=11;',11,None),
 'recursive-growth':(chain(64),13,None),
 'depth-error':(chain(140),None,'Reference chain too deep'),
}
runs=0
for name,(source,value,error) in cases.items():
 d=root/name;d.mkdir(parents=True,exist_ok=True);(d/'types.h').write_text(header);(d/'source.cpp').write_text(source)
 project=d/'project.json';project.write_text(json.dumps(dict(version=1,name=name,project=[dict(name='types',type='header',path='types.h'),dict(name='source',type='source',path='source.cpp')],preprocessor=dict(predefines=[]))))
 results=[]
 for indexed in (0,1):
  for reverse in (0,1):
   report=d/f'{indexed}-{reverse}.json';env=dict(os.environ,PLS09_INDEXED=str(indexed),PLS03_MODE='1',PLS04_EARLY='1',PLS06_DENSE='1',PLS07_OWN='1',PLS07_VERIFY='1',PLS08_REVERSE=str(reverse))
   r=subprocess.run([str(exe),str(project),str(report)]+(['--reverse'] if reverse else []),capture_output=True,text=True,env=env,timeout=30)
   actual=json.loads(report.read_text());assert (r.returncode==0)==(error is None),(name,indexed,r.stderr)
   if error:assert error in actual['error'],actual
   else:
    assert actual['values'][0]['value']==value and len(actual['warnings'])==1,actual
    assert actual['graph_writer']['runtime_fields']['b.x']==value
   results.append(actual);runs+=1
 for actual in results[1:]:
  for key in (('error','origin','order') if error else ('semantic_fingerprint','canonical_numbering_fingerprint','values','warnings')):assert actual[key]==results[0][key],(name,key)
print(f'B1: {runs} retry-context/recursive-growth/depth and exact numbering/origin parity cases passed')
