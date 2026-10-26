"""Reuse temporal oracles, replacing the obsolete file-local visibility rule."""
from pathlib import Path
source=(Path(__file__).resolve().parents[1]/'parallel-local-source-10-c3/test.py').read_text()
old="'missing-file-declaration':(header,['I b;','b.x=7;'],'declaration or extern',[],None),"
assert source.count(old)==1
source=source.replace(old,"'project-visible':(header,['I b;','b.x=7;'],{'b.x':7},[],0),")
extra="""cases.update({
 'forward-object':(header,['b.x=10;','I b;'],{'b.x':10},[],0),
 'forward-bind-target':(header,['R r; r.ref=b; r.ref.x=11;','I b;'],{'b.x':11},[('r.ref','b')],0),
 'forward-initializer':(header,['n=10;','int n=3;'],{'n':3},[],1),
 'late-initializer':(header,['int n=3;','n=10;'],{'n':10},[],1),
 'duplicate-cross-file':(header,['I b;','I b;'],'Duplicate object definition',[],None),
 'type-conflict':(header,['I b;','int b;'],'type conflict',[],None),
 'extern-type-conflict':(header,['extern I b;','int b;'],'type conflict',[],None),
 'repeated-extern':(header,['extern I b; extern I b;','I b;','b.x=12;'],{'b.x':12},[],0),
 'unknown-object':(header,['missing.x=1;'],'Unknown object',[],None),
 'error-before-duplicate':(header,['I b; b.a[123]=2;','I b;'],'member/index',[],None),
 'namespace-forward':('namespace N { struct I { int x=4; }; }',['namespace N { b.x=9; }','namespace N { I b; }'],{'N::b.x':9},[],0),
 'reference-copy-current':(header,['I b; I c; I& p; I& q; q=b; p=q; q=c; p.x=10;'],{'b.x':10,'c.x':5},[('p','b'),('q','c')],1),
})
"""
assert source.count('samples=[]')==1
exec(compile(source.replace('samples=[]',extra+'samples=[]'),str(Path(__file__)), 'exec'))
