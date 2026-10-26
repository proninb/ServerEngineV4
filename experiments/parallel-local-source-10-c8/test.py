import hashlib,os,subprocess,sys
from pathlib import Path
exe,unit,root=map(lambda p:Path(p).resolve(),sys.argv[1:]);root.mkdir(parents=True,exist_ok=True)
base=None
for single in (0,1):
 env=dict(os.environ,PLS10_C8_SINGLE=str(single),PLS10_C7_FAST='1')
 r=subprocess.run([str(unit)],env=env,capture_output=True,text=True,timeout=60);assert r.returncode==0,r.stderr
 if base is None:base=r.stdout
 else:assert base==r.stdout,'link records/handles differ across strategies'
 (root/f'links-{single}.stdout').write_text(r.stdout)
script=Path(__file__).resolve().parents[1]/'parallel-local-source-10-c7/test.py';baseline=None
for single in (0,1):
 env=dict(os.environ,PLS10_C8_SINGLE=str(single))
 # Reuse all C7 cold runtime/path/diagnostic oracles; no C7 legacy audit overhead.
 r=subprocess.run([sys.executable,str(script),str(exe),str(exe),str(root/'shared')],env=env,capture_output=True,text=True,timeout=180)
 (root/f'mode-{single}.stdout').write_text(r.stdout);(root/f'mode-{single}.stderr').write_text(r.stderr);assert r.returncode==0,(single,r.stderr[-2500:])
 images={str(p.relative_to(root/'shared')):hashlib.sha256(p.read_bytes()).hexdigest() for p in (root/'shared').rglob('*.bin')}
 errors={str(p.relative_to(root/'shared')):p.read_text() for p in (root/'shared').rglob('*.stderr') if p.read_text()}
 if baseline is None:baseline=(images,errors)
 else:assert baseline==(images,errors),'binding strategy changed artifacts/diagnostics'
print('768 native C7 oracle runs plus link insertion/replacement/strict-add/rehash parity PASS')
