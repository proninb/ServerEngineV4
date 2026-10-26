"""Final-binding context dependencies: stable seed, restoration and oscillation."""
import json,os,subprocess,sys
from pathlib import Path
exe,root=map(lambda p:Path(p).resolve(),sys.argv[1:]);root.mkdir(parents=True,exist_ok=True)
header='struct N { N& next; int x=5; };'
cases={
 'seed-policy':(['N a; N b; a.next=a; b.next=a; b.next.next=b;'],None,{'a.next':'b','b.next':'a'},1),
 'oscillation':(['N a; N b; a.next=a; b.next=b; a.next.next=b;'],'Nested reference target linking did not converge',{},None),
 'extern-local':(['N a; N b; a.next=b;','extern N a; extern N b; b.x=1; a.next.x=2;'],None,{'a.next':'b'},1),
}
runs=0
for name,(sources,error,pointers,warnings) in cases.items():
 d=root/name;d.mkdir(parents=True,exist_ok=True);(d/'types.h').write_text(header)
 entries=[dict(name='types',type='header',path='types.h')]
 for i,source in enumerate(sources):(d/f'{i}.cpp').write_text(source);entries.append(dict(name=str(i),type='source',path=f'{i}.cpp'))
 project=d/'project.json';project.write_text(json.dumps(dict(version=1,name=name,project=entries,preprocessor=dict(predefines=[]))))
 reports=[]
 for final in (0,1,2):
  for reverse in (0,1):
   report=d/f'{final}-{reverse}.json';env=dict(os.environ,PLS10_FINALIZE=str(final),PLS09_INDEXED='1',PLS03_MODE='1',PLS04_EARLY='1',PLS06_DENSE='1',PLS07_OWN='1',PLS07_VERIFY='1',PLS08_REVERSE=str(reverse))
   r=subprocess.run([str(exe),str(project),str(report)]+(['--reverse'] if reverse else []),capture_output=True,text=True,env=env,timeout=30)
   actual=json.loads(report.read_text());assert (r.returncode==0)==(error is None),(name,final,r.stderr)
   if error:assert error in actual['error'],actual
   else:
    g=actual['graph_writer'];assert len(actual['warnings'])==warnings
    for target,source in pointers.items():assert g['runtime_pointers'][target]==g['runtime_addresses'][source],(name,target,source,g)
    if name=='extern-local':assert g['runtime_fields']['b.x']==2
   reports.append(actual);runs+=1
 for actual in reports[1:]:
  for key in (('error','origin','order') if error else ('semantic_fingerprint','canonical_numbering_fingerprint','values','warnings')):assert actual[key]==reports[0][key],(name,key)
print(f'PLS10-A: {runs} seed-policy, oscillation and explicit file-local extern cases passed')
