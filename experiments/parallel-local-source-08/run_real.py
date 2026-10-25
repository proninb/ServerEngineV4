"""Two correctness/materialization runs; explicitly not a PUBLISH benchmark."""
import argparse,json,os,subprocess,time
from pathlib import Path
repo=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('output',type=Path);p.add_argument('--project',type=Path,default=repo/'build/publish-subsecond-01/fixture/project.json');a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);project=a.project.resolve();exe=repo/'build/parallel-local-source-08/Release/PublishGraphWriter08.exe'
oracle=project.parent/'.serverengine'/project.name/'compiled.bin';reports=[]
for reverse in (False,True):
 report=out/('reverse.json' if reverse else 'forward.json')
 env=dict(os.environ,PLS03_MODE='1',PLS04_EARLY='1',PLS06_DENSE='1',PLS07_OWN='1',PLS07_VERIFY='1',PLS08_REVERSE=str(int(reverse)))
 cmd=[str(exe),str(project),str(report)]+(['--reverse'] if reverse else [])+(['--oracle',str(oracle)] if oracle.exists() else [])
 start=time.perf_counter();r=subprocess.run(cmd,capture_output=True,text=True,env=env);wall=(time.perf_counter()-start)*1000
 report.with_suffix('.stdout').write_text(r.stdout);report.with_suffix('.stderr').write_text(r.stderr);assert r.returncode==0,(reverse,r.stderr)
 d=json.loads(report.read_text());g=d['graph_writer'];assert g['query_values']==g['runtime_values']==d['counts']['cells']
 assert g['query_links']==g['runtime_links'] and d['counts']['linker_token_reads']==0 and g['full_publish_benchmark'] is False
 if project==repo/'build/publish-subsecond-01/fixture/project.json':
  assert d['semantic_fingerprint']=='13649286433448276567' and g['objects']==137584 and g['query_values']==705238 and g['query_links']==367144
 reports.append(dict(reverse=reverse,validation_process_ms=wall,graph=g,fingerprint=d['semantic_fingerprint'],legacy_projection_oracle=d['oracle_projection_checked']))
 print(('reverse' if reverse else 'forward'),g['objects'],g['initializations'],g['links'],g['query_values'],g['runtime_links'],flush=True)
assert reports[0]['fingerprint']==reports[1]['fingerprint']
for key in ('objects','links','initializations','cold_declarations','query_values','query_links','runtime_values','runtime_links','disjoint_source_ranges'):
 assert reports[0]['graph'][key]==reports[1]['graph'][key],key
(out/'summary.json').write_text(json.dumps(dict(full_publish_benchmark=False,runs=reports),indent=2))
