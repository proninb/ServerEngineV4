"""Semantic and persisted-image parity across off/counters/sampling observers."""
import hashlib,json,os,subprocess,sys
from pathlib import Path
exe,root=map(lambda p:Path(p).resolve(),sys.argv[1:]);root.mkdir(parents=True,exist_ok=True)
script=Path(__file__).resolve().parents[1]/'parallel-local-source-10-c5/test.py'
baseline=None
for mode in (0,1,3):
 directory=root/'shared';env=dict(os.environ,PLS10_C6=str(mode),PLS10_C6_REPORT=str(root/f'probe-{mode}.json'))
 r=subprocess.run([sys.executable,str(script),str(exe),str(directory)],env=env,capture_output=True,text=True,timeout=120)
 (root/f'mode-{mode}.stdout').write_text(r.stdout);(root/f'mode-{mode}.stderr').write_text(r.stderr);assert r.returncode==0,(mode,r.stdout,r.stderr)
 images={str(p.relative_to(directory)):hashlib.sha256(p.read_bytes()).hexdigest() for p in directory.rglob('*.bin')}
 if baseline is None:baseline=images
 else:assert baseline==images,'observer changed persisted output'
env=dict(os.environ,PLS08_CANDIDATE='1',PLS10_C6='1',PLS10_WORKERS='4',PLS10_C6_REPORT=str(root/'triple.json'))
r=subprocess.run([str(exe),'publish',str(root/'shared/triple/project.json'),'0'],env=env,capture_output=True,text=True,timeout=60)
assert r.returncode==0,r.stderr
d=json.loads((root/'triple.json').read_text())['tags']
assert d['value_lookup']['calls']==3 and d['value_lookup']['hits']==2 and d['value_lookup']['misses']==1,d['value_lookup']
assert d['origin']['hits']==2 and d['origin']['misses']==1 and d['warning']['calls']==2
assert d['link_lookup']['hits']==1 and d['link_lookup']['misses']==2,d['link_lookup']
print('288 native semantic runs (off/counts/independent sampling), observer image parity and exact hit/miss fixture PASS')
