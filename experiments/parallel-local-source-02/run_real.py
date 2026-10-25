"""Correctness series on real UnitProXL, not a full-PUBLISH benchmark."""
import argparse,hashlib,json,subprocess
from pathlib import Path
repo=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('output',type=Path)
p.add_argument('--exe',type=Path,default=repo/'build/parallel-local-source-02/Release/ParallelLocalSource02.exe')
p.add_argument('--project',type=Path,default=repo/'build/publish-subsecond-01/fixture/project.json')
p.add_argument('--oracle',type=Path,default=repo/'build/publish-subsecond-01/fixture/.serverengine/project.json/compiled.bin')
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
metadata=dict(executable=str(a.exe.resolve()),executable_sha256=hashlib.sha256(a.exe.read_bytes()).hexdigest(),project=str(a.project.resolve()),oracle=str(a.oracle.resolve()))
results=[]
for reverse in (False,True):
 name='reverse' if reverse else 'forward';report=out/(name+'.json')
 r=subprocess.run([str(a.exe.resolve()),str(a.project.resolve()),str(report),'--oracle',str(a.oracle.resolve())]+(['--reverse'] if reverse else []),capture_output=True,text=True)
 (out/(name+'.stdout')).write_text(r.stdout);(out/(name+'.stderr')).write_text(r.stderr)
 assert r.returncode==0,(name,r.stdout,r.stderr)
 result=json.loads(report.read_text());assert result['oracle_projection_checked']
 assert result['counts']['linker_token_reads']==0
 if a.project.resolve()==(repo/'build/publish-subsecond-01/fixture/project.json').resolve():
  assert len(result['tasks'])==3373 and result['counts']['declarations']==137584
  assert result['counts']['writes']==705238 and result['counts']['bindings']==367144
  assert result['endpoint_requests']==dict(extracted=946656,typed=946656)
 results.append(result)
assert results[0]['semantic_fingerprint']==results[1]['semantic_fingerprint']
assert results[0]['counts']==results[1]['counts']
metadata['counts']=results[0]['counts'];metadata['endpoint_requests']=results[0]['endpoint_requests']
metadata['semantic_fingerprint']=results[0]['semantic_fingerprint']
metadata['forward_timings_ms']=results[0]['timings_ms'];metadata['reverse_timings_ms']=results[1]['timings_ms']
(out/'summary.json').write_text(json.dumps(metadata,indent=2))
print(json.dumps(metadata,indent=2))
