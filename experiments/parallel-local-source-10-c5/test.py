from pathlib import Path
source=(Path(__file__).resolve().parents[1]/'parallel-local-source-10-c3/test.py').read_text()
old="'missing-file-declaration':(header,['I b;','b.x=7;'],'declaration or extern',[],None),"
assert source.count(old)==1
source=source.replace(old,"'project-visible':(header,['I b;','b.x=7;'],{'b.x':7},[],0),")
source=source.replace('samples=[]',"""cases.update({
 'requires-future':(header,['b.x=10;','I b;'],'not-yet-declared',[],None),
 'unknown':(header,['unknown.x=1;'],'Unknown',[],None),
})
samples=[]""")
exec(compile(source,str(Path(__file__)), 'exec'))
