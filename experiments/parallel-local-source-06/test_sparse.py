"""Explicit holes, late preexisting cells, duplicate chains and empty writes."""
import json,os,subprocess,sys
from pathlib import Path
exe,root=map(lambda p:Path(p).resolve(),sys.argv[1:]);root.mkdir(parents=True,exist_ok=True)
cases={
 'late-existing':('I b; int& r; r=b.x; I z; z.y=7; b.x=1; b.x=2;',[2,7],1),
 'large-gap':('I b; '+''.join(f'int& p{i}; p{i}=b.x; ' for i in range(257))+'I z; z.y=7; b.x=1; b.x=4;',[4,7],1),
 'alias-duplicates':('I b; I& r; int& s; r=b; s=b.x; I z; z.y=7; b.x=1; r.x=2; b.x=3; z.y=8;',[3,8],3),
 'no-writes':('I b; I& r; r=b;',[],0),
}
count=0
for name,(source,expected,warnings) in cases.items():
 d=root/name;d.mkdir(parents=True,exist_ok=True);(d/'types.h').write_text('struct I { int x; int y; };')
 # Split tasks so reverse extraction exercises a different execution order.
 split=source.index('I z;') if 'I z;' in source else len(source)
 (d/'a.cpp').write_text(source[:split]);(d/'b.cpp').write_text(source[split:])
 project=d/'project.json';project.write_text(json.dumps(dict(version=1,name=name,project=[dict(name='types',type='header',path='types.h'),dict(name='a',type='source',path='a.cpp'),dict(name='b',type='source',path='b.cpp')],preprocessor=dict(predefines=[]))))
 results=[]
 for mode in (0,1):
  for reverse in (False,True):
   report=d/f'{mode}-{int(reverse)}.json';heads=report.with_suffix('.heads.json')
   env=dict(os.environ,PLS03_MODE='1',PLS04_EARLY='1',PLS06_DENSE=str(mode),PLS06_HEADS_PATH=str(heads))
   run=subprocess.run([str(exe),str(project),str(report)]+(['--reverse'] if reverse else []),capture_output=True,text=True,env=env)
   assert run.returncode==0,(name,mode,reverse,run.stderr)
   result=json.loads(report.read_text());h=json.loads(heads.read_text());assert sorted(v['value'] for v in result['values'])==expected,(name,result)
   assert len(result['warnings'])==warnings and result['counts']['linker_token_reads']==0
   assert h['initial_cells']>0 and h['populated']==len(expected)
   if mode and expected:assert h['slots']>h['populated'],(name,h)
   if not expected:assert h['slots']==0
   results.append(result);count+=1
 for result in results[1:]:
  for key in ('values','warnings','semantic_fingerprint'):assert result[key]==results[0][key],(name,key)
print(f'PLS06: {count} sparse/preexisting-cell cases passed')
