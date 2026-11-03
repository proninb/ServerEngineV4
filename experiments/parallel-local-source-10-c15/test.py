import hashlib,os,subprocess,sys
from pathlib import Path
native,audit,root=map(lambda p:Path(p).resolve(),sys.argv[1:]);root.mkdir(parents=True,exist_ok=True)
script=Path(__file__).resolve().parents[1]/'parallel-local-source-10-c7/test.py';program=script.read_text()
extra="""
 'declaration-namespace-shadow':('struct I { int x=5; };',['I a; namespace N { I a; a.x=3; } a.x=2;'],{'a.x':2,'N::a.x':3},[],0),
 'declaration-reopen-namespace':('struct I { int x=5; };',['namespace N { extern I a; } namespace N { I a; a.x=7; }'],{'N::a.x':7},[],0),
"""
needle='extra="""cases.update({';assert program.count(needle)==1
program=program.replace(needle,needle+extra).replace("print('384 native oracle runs:","print('408 native oracle runs:")
wrapper=root/'invoke.py';wrapper.write_text('__file__='+repr(str(script))+'\n'+program);old=None
for mode in ('0','1'):
 env=dict(os.environ,PLS10_C10_REUSE='1',PLS10_C15_DIRECT=mode)
 p=subprocess.run([sys.executable,str(wrapper),str(native),str(audit),str(root/'common')],env=env,capture_output=True,text=True,timeout=300)
 (root/f'{mode}.stdout').write_text(p.stdout);(root/f'{mode}.stderr').write_text(p.stderr);assert p.returncode==0,p.stderr[-3000:]
 d=root/'common/shared';state=({str(p.relative_to(d)):hashlib.sha256(p.read_bytes()).hexdigest() for p in d.rglob('*.bin')},{str(p.relative_to(d)):p.read_text() for p in d.rglob('*.stderr') if p.read_text()},{str(p.relative_to(d)):[s for s in p.read_text().splitlines() if s.startswith('ordered_warning=')] for p in d.rglob('*.stdout')})
 if old is None:old=state
 else:assert old==state,'declaration WHO changed bytes/errors/origins'
 print(mode,p.stdout.strip(),flush=True)
print('816 declaration WHO oracle runs PASS')
