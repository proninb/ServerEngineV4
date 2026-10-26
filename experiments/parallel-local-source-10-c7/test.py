"""Same paths, scheduling/strategy/audit parity and independent Runtime oracles."""
import hashlib,json,os,subprocess,sys
from pathlib import Path
native,audit,root=map(lambda p:Path(p).resolve(),sys.argv[1:]);root.mkdir(parents=True,exist_ok=True)
script=Path(__file__).resolve().parents[1]/'parallel-local-source-10-c5/test.py'
# Execute the existing oracle definitions, adding focused view-lifetime/domain cases.
source=script.read_text()
source=source.replace("exec(compile(source,str(Path(__file__)), 'exec'))", "source=source.replace('samples=[]',extra+'samples=[]');exec(compile(source,str(Path(__file__)), 'exec'))")
extra="""cases.update({
 'view-lifetime-growth':('struct T { char text[128]; };',['T t[40];'+''.join(f't[{i}].text="abc"; t[{i}].text="xyz";' for i in range(40))],{'t[0].text[0]':120,'t[0].text[1]':121,'t[0].text[2]':122,'t[39].text[127]':0},[],5120),
 'nested-union':('union U { int x; int y; }; struct T { U u; };',['T t; t.u.x=1;'],'union storage',[],None),
 'union-string':('union U { char text[4]; int x; };',['U u; u.text="abc";'],'union storage',[],None),
 'unbound-before-union':('union U { int x; }; struct R { U& ref; };',['R r; r.ref.x=1;'],'unbound',[],None),
 'reference-copy-current':(header,['I b; I c; I& p; I& q; q=b; p=q; q=c; p.x=10;'],{'b.x':10,'c.x':5},[('p','b'),('q','c')],1),
 'volatile-root':('struct Empty {};',['volatile int n=1; n=2;'],{'n':2},[],1),
 'const-member':('struct T { const int n=3; };',['T t; t.n=4;'],'const initialization',[],None),
 'virtual-base':('struct A { int x=1; }; struct B:virtual A {};',['B b; b.x=3;'],'Virtual base classes',[],None),
})
"""
oracle=root/'oracle.py';oracle.write_text('extra='+repr(extra)+'\n'+source)
baseline=None
for label,exe,fast in [('off',native,0),('fast',native,1),('audit',audit,1)]:
 env=dict(os.environ,PLS10_C7_FAST=str(fast));env.pop('PLS10_C6',None)
 # oracle uses the catalog-relative __file__ to load C3, so pass its source through
 # a wrapper with the original experiment location while retaining common paths.
 wrapper=root/'invoke.py';wrapper.write_text('from pathlib import Path\nprogram=Path('+repr(str(oracle))+').read_text()\n__file__='+repr(str(script))+'\nexec(compile(program,__file__,"exec"))\n')
 r=subprocess.run([sys.executable,str(wrapper),str(exe),str(root/'shared')],env=env,capture_output=True,text=True,timeout=180)
 (root/f'{label}.stdout').write_text(r.stdout);(root/f'{label}.stderr').write_text(r.stderr)
 assert r.returncode==0,(label,r.stderr[-3000:])
 images={str(p.relative_to(root/'shared')):hashlib.sha256(p.read_bytes()).hexdigest() for p in (root/'shared').rglob('*.bin')}
 errors={str(p.relative_to(root/'shared')):p.read_text() for p in (root/'shared').rglob('*.stderr') if p.read_text()}
 traces={str(p.relative_to(root/'shared')):[line for line in p.read_text().splitlines() if line.startswith('ordered_warning=')] for p in (root/'shared').rglob('*.stdout')}
 signature=(images,errors,traces)
 if baseline is None:baseline=signature
 else:assert baseline==signature,('strategy changed persisted output or diagnostics',label)
print('384 native oracle runs: fast/off/audit, scheduling, lifetime growth and exact diagnostics PASS')
