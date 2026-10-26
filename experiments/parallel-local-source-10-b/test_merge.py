"""Exact direct/indirect LWW and warning transition order, never just counts."""
import json,os,subprocess,sys
from pathlib import Path
exe,root=map(lambda p:Path(p).resolve(),sys.argv[1:]);root.mkdir(parents=True,exist_ok=True)
header='struct I { int x=5; char text[4]; }; struct R { I& ref; };'
cases={
 'between':(['I c; R a; c.x=1; a.ref.x=2; c.x=3; a.ref=c;'],3,2),
 'indirect-wins':(['I c; R a; c.x=1; a.ref.x=2; c.x=3; a.ref=c; a.ref.x=4;'],4,3),
 'indirect-first':(['I c; R a; a.ref.x=1; c.x=2; c.x=3; a.ref=c;'],3,2),
 'rebind':(['I b; I c; R a; a.ref=b; a.ref.x=1; b.x=2; a.ref=c; c.x=3; a.ref.x=4;'],None,3),
 'string-ties':(['I c; R a; a.ref=c; a.ref.text="ab"; c.text[1]=120; c.text="xy";'],None,5),
 'cross-file':(['I c; R a; c.x=1; a.ref=c;','extern I c; extern R a; a.ref.x=2; c.x=3;'],3,2),
 'direct-stress':(['I c;'+''.join(f'c.x={i};' for i in range(1000))],999,999),
}
runs=0
for name,(sources,value,warnings) in cases.items():
 d=root/name;d.mkdir(parents=True,exist_ok=True);(d/'types.h').write_text(header)
 entries=[dict(name='types',type='header',path='types.h')]
 for i,text in enumerate(sources):(d/f'{i}.cpp').write_text(text);entries.append(dict(name=str(i),type='source',path=f'{i}.cpp'))
 project=d/'project.json';project.write_text(json.dumps(dict(version=1,name=name,project=entries,preprocessor=dict(predefines=[]))))
 reports=[]
 for compact in (0,1):
  for reverse in (0,1):
   report=d/f'{compact}-{reverse}.json';env=dict(os.environ,PLS10_WRITES=str(compact),PLS10_FINALIZE='2',PLS09_INDEXED='1',PLS03_MODE='1',PLS04_EARLY='1',PLS06_DENSE='1',PLS07_OWN='1',PLS07_VERIFY='1',PLS08_REVERSE=str(reverse))
   r=subprocess.run([str(exe),str(project),str(report)]+(['--reverse'] if reverse else []),capture_output=True,text=True,env=env,timeout=60)
   assert r.returncode==0,(name,compact,r.stderr);actual=json.loads(report.read_text());assert len(actual['warnings'])==warnings,(name,len(actual['warnings']))
   if value is not None:assert actual['graph_writer']['runtime_fields']['c.x']==value
   if name=='between':
    text=sources[0];assert [(w['previous']['offset'],w['current']['offset']) for w in actual['warnings']]==[(text.index('c.x=1'),text.index('a.ref.x=2')),(text.index('a.ref.x=2'),text.index('c.x=3'))]
   if name=='string-ties':assert [actual['graph_writer']['runtime_fields'][f'c.text[{i}]'] for i in range(4)]==[120,121,0,0]
   if name=='rebind':assert actual['graph_writer']['runtime_fields']['b.x']==2 and actual['graph_writer']['runtime_fields']['c.x']==4
   reports.append(actual);runs+=1
 for actual in reports[1:]:
  for key in ('semantic_fingerprint','canonical_numbering_fingerprint','values','warnings'):assert actual[key]==reports[0][key],(name,key)
print(f'PLS10-B: {runs} direct/indirect event interleaving, exact warning ties, rebind and sparse history cases passed')
