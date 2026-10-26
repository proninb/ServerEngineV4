"""Both scratch strategies against the existing independent C7 runtime oracles."""
import hashlib,os,subprocess,sys
from pathlib import Path
native,audit,root=map(lambda p:Path(p).resolve(),sys.argv[1:]);root.mkdir(parents=True,exist_ok=True)
script=Path(__file__).resolve().parents[1]/'parallel-local-source-10-c7/test.py'
program=script.read_text()
extra="""
 'scratch-deep-shallow-root':('struct I { int x=5; }; struct N { I child; };',['N a; I b; int n=1; a.child.x=2; b.x=3; n=4; a.child.x=6; b.x=7;'],{'a.child.x':6,'b.x':7,'n':4},[],3),
 'scratch-cross-file-reset':('struct I { int x=5; }; struct N { I child; };',['N a; I b; int n=1; a.child.x=2;','b.x=3; n=4; a.child.x=6; b.x=7;'],{'a.child.x':6,'b.x':7,'n':4},[],3),
"""
needle='extra="""cases.update({'
assert program.count(needle)==1
program=program.replace(needle,needle+extra)
program=program.replace("print('384 native oracle runs:","print('408 native oracle runs:")
wrapper=root/'invoke.py';wrapper.write_text('__file__='+repr(str(script))+'\n'+program)
baseline=None
for mode in ('0','1'):
 env=dict(os.environ,PLS10_C10_REUSE=mode)
 common=root/'common'
 r=subprocess.run([sys.executable,str(wrapper),str(native),str(audit),str(common)],env=env,capture_output=True,text=True,timeout=300)
 (root/f'{mode}.stdout').write_text(r.stdout);(root/f'{mode}.stderr').write_text(r.stderr)
 assert r.returncode==0,(mode,r.stderr[-2500:]);print(mode,r.stdout.strip(),flush=True)
 shared=common/'shared'
 images={str(p.relative_to(shared)):hashlib.sha256(p.read_bytes()).hexdigest() for p in shared.rglob('*.bin')}
 errors={str(p.relative_to(shared)):p.read_text() for p in shared.rglob('*.stderr') if p.read_text()}
 traces={str(p.relative_to(shared)):[s for s in p.read_text().splitlines() if s.startswith('ordered_warning=')] for p in shared.rglob('*.stdout')}
 signature=(images,errors,traces)
 if baseline is None:baseline=signature
 else:assert baseline==signature,'scratch changed persisted fixture output or diagnostics'
print('816 oracle runs PASS; off/on scratch, fast/fallback, scheduling, lifetime, diagnostics, deep/shallow/root and cross-file reset')
