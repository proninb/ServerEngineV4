"""Original negative Source oracles must survive both diagnostic observers."""
import json,os,subprocess,sys
from pathlib import Path
build,fixtures=map(lambda p:Path(p).resolve(),sys.argv[1:]);count=0
env=dict(os.environ,PLS08_CANDIDATE='1',PLS03_MODE='1',PLS04_EARLY='1',PLS06_DENSE='1')
env.pop('PLS03_PROFILE_PATH',None)
for report in sorted(fixtures.glob('*/forward.json')):
 data=json.loads(report.read_text())
 if 'error' not in data:continue
 for name in ('PublishAliasPhases09','PublishAliasDetails09'):
  r=subprocess.run([str(build/'Release'/f'{name}.exe'),'publish',str(report.parent/'project.json'),'0'],capture_output=True,text=True,env=env,timeout=30)
  assert r.returncode!=0 and data['error'] in r.stderr,(name,report,r.stdout,r.stderr)
  count+=1
assert count>0
print(f'PLS09: {count} original negative Source oracle invocations passed')
