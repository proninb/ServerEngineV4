"""PLS08-B native persistence/resident oracle; cold checks are outside timers."""
import json,os,subprocess,sys
from pathlib import Path
exe,fixtures=map(lambda p:Path(p).resolve(),sys.argv[1:])
cases=json.loads((fixtures/'summary.json').read_text());runs=[]
for case in cases:
 name=case['case'];directory=fixtures/name
 env=dict(os.environ,PLS03_MODE='1',PLS04_EARLY='1',PLS06_DENSE='1',PLS08_CANDIDATE='1',PLS08_COLD_CHECK='1',PLS08_OBJECT_AUDIT='1')
 # PUBLISH operates only in the private fixture tree, never original projects.
 r=subprocess.run([str(exe),'publish',str(directory/'project.json'),'0'],capture_output=True,text=True,env=env,timeout=60)
 (directory/'resident.stdout').write_text(r.stdout);(directory/'resident.stderr').write_text(r.stderr)
 assert r.returncode==0,(name,r.stdout,r.stderr)
 line=next(line for line in r.stdout.splitlines() if line.startswith('pls08_resident='))
 actual=json.loads(line.split('=',1)[1])
 for field,value in case['values'].items():assert actual['fields'][field]==value,(name,field,actual)
 for target,source in case['bindings']:assert actual['pointers'][target]==actual['addresses'][source],(name,target,source,actual)
 assert 'pls08_candidate=1' in r.stdout and 'pls08_full_with_teardown_ms=' in r.stdout
 assert 'pls08_object_storage_sha256=' in r.stdout
 artifacts=directory/'.serverengine'/'project.json'
 assert (artifacts/'compiled.bin').is_file() and (artifacts/'runtime.bin').is_file(),(name,artifacts)
 assert not any((artifacts/f).exists() for f in ('source.bin','database.bin','project.manifest')),name
 runs.append(dict(case=name,mode='native_PUBLISH',resident=actual))
(fixtures/'resident-summary.json').write_text(json.dumps(runs,indent=2))
print(f'PLS08-B: {len(runs)} native PUBLISH/persistence/resident SHM oracles passed')
