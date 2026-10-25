"""Baseline cost breakdown only; no candidate is selected by this runner."""
import json,os,subprocess,sys
from pathlib import Path
repo=Path(__file__).resolve().parents[2]
out=Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=True)
project=repo/'build/publish-subsecond-01/fixture/project.json'
oracle=project.parent/'.serverengine'/project.name/'compiled.bin'
for name,audit,reverse in [('timing0',False,False),('timing1',False,False),('timing2',False,False),('audit',True,False),('audit-reverse',True,True)]:
 exe=repo/('build/parallel-local-source-03/Release/ParallelLocalSource03'+('Audit' if audit else '')+'.exe')
 report=out/(name+'.json');profile=out/(name+'.profile.json')
 r=subprocess.run([str(exe),str(project),str(report),'--oracle',str(oracle)]+(['--reverse'] if reverse else []),capture_output=True,text=True,
  env=dict(os.environ,PLS03_PROFILE_PATH=str(profile),PLS03_MODE='0'))
 (out/(name+'.stdout')).write_text(r.stdout);(out/(name+'.stderr')).write_text(r.stderr)
 assert r.returncode==0,(name,r.stdout,r.stderr)
 data=json.loads(report.read_text());assert data['semantic_fingerprint']=='13649286433448276567',data['semantic_fingerprint']
 assert data['oracle_projection_checked'] and data['counts']['linker_token_reads']==0
 print(name,data['timings_ms']['aliases'],data['lifecycle_ms'],flush=True)
