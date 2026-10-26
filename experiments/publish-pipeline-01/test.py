from pathlib import Path
import hashlib,json,os,subprocess,sys
repo=Path(__file__).resolve().parents[2];out=Path(sys.argv[1]).resolve();exe=out/'build/Release/ServerEngineV4PublishBenchmark.exe'
cases=out/'cases';cases.mkdir(exist_ok=True);logs=out/'functional-logs';logs.mkdir(exist_ok=True)
base={k:v for k,v in os.environ.items() if not k.startswith(('PUBLISH_PIPELINE_01_','PLS','CW_'))}
fixtures={
 'features':('''enum class Mode : unsigned int { Idle = 1, Busy = 2 };
struct Base { int x = 3; };
struct Thing : Base { int value = 2; char label[4]; int& input; Thing() : value(7), input(value) {} };
''','''Thing a; Thing b; a.input = b.value; a.label = "abc";
extern int forward; int forward = 23; int scalar = 17; int zero;
''',3),
 'nested':('''struct Leaf { int value = 4; int& input; }; struct Root { Leaf values[2]; char text[6]; };
''','''Root a; Root b; a.values[0].input = b.values[1].value; b.values[0].value = 29; a.text = "hello";
''',2),
 'late_binding':('''struct Node { int value = 3; int& input; };
''','''Node a; Node b; Node c; a.input = b.value; a.input = c.value; b.value = 10; c.value = 20;
''',1),
 'constructor':('''struct Child { int value = 2; Child() : value(7) {} }; struct Parent { Child nested; int n; Parent() : n(4) {} };
''','''Parent first; Parent second; first.n = 15;
''',2),
}
negative={
 'parse_error':('struct Broken { int value;','',1),
 'reserved':('struct Node { int value; };','Node System;',1),
 'reference_cycle':('struct Node { int& input; };','Node a; Node b; a.input = b.input; b.input = a.input;',1),
 'extern_mismatch':('', 'extern int value; long value;',0),
}
for name,(header,source,count) in (fixtures|negative).items():
 root=cases/name;root.mkdir(exist_ok=True);(root/'types.hpp').write_text(header);(root/'objects.cpp').write_text(source)
 (root/'project.json').write_text(json.dumps(dict(version=1,name=name,project=[dict(name='types',type='header',path='types.hpp'),dict(name='objects',type='source',path='objects.cpp')],preprocessor=dict(predefines=[]))))

def sha(path):
 with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
representation=json.loads((out/'snapshot.json').read_text()).get('representation','owned')
baseline_lanes={}
results=[]
def run(case,arm,operation='publish-profile',fault=None,expected=False,fallback=False):
 artifact=out/'functional-artifacts'/case/arm
 env=dict(base,PUBLISH_PIPELINE_01_MODE=arm,PUBLISH_PIPELINE_01_ARTIFACT_ROOT=str(artifact),PUBLISH_PIPELINE_01_SHM_AUDIT='1')
 if fault:env['PUBLISH_PIPELINE_01_FAIL']=fault
 if expected:env['PUBLISH_PIPELINE_01_EXPECT_FAILURE']='1'
 if fallback:env['PUBLISH_PIPELINE_01_FORCE_SERIAL']='1'
 count=(fixtures|negative)[case][2]
 r=subprocess.run([str(exe),operation,str(cases/case/'project.json'),str(count)],env=env,capture_output=True,text=True,timeout=90)
 tag=f'{case}-{arm}-{operation}-{fault or "normal"}-fallback{int(fallback)}'
 (logs/(tag+'.stdout')).write_text(r.stdout);(logs/(tag+'.stderr')).write_text(r.stderr)
 assert r.returncode==0,(tag,r.returncode,r.stderr[-5000:],r.stdout[-1000:])
 fields=dict(item.split('=',1) for item in r.stdout.strip().split(',') if '=' in item)
 if expected:
  assert fields.get('pipeline_expected_failure')=='1',tag
  hashes=None
 else:
  hashes={name:sha(artifact/name) for name in ('compiled.bin','runtime.bin')};hashes['shm']=fields['pipeline_shm_sha256']
  if operation.startswith('publish') and arm=='baseline': baseline_lanes[case]=(fields['pipeline_object_lanes'],fields['pipeline_mark_lanes'])
  if operation.startswith('publish') and arm!='baseline':
   assert fields['pipeline_type_adoptions']=='1' and fields['pipeline_prepared_consumers']=='1' and fields['pipeline_consumer_success']=='1',tag
   if representation=='compact':assert (fields['pipeline_object_lanes'],fields['pipeline_mark_lanes'])==baseline_lanes[case],tag
   if arm=='serial' or fallback:assert float(fields['pipeline_overlap_ms'])==0 and fields['pipeline_lanes']=='1'
 results.append(dict(tag=tag,hashes=hashes,expected_failure=expected,fields=fields));print('PASS',tag,flush=True)
 return hashes
for case in fixtures:
 gold=run(case,'baseline')
 for arm in ('baseline','serial','overlap'):
  if arm!='baseline':assert run(case,arm)==gold,(case,arm)
  assert run(case,arm,'audit')==gold,(case,arm,'audit')
  assert run(case,arm,'load')==gold,(case,arm,'load')
 assert run(case,'overlap',fallback=True)==gold,(case,'fallback')
for case in negative:
 for arm in ('baseline','serial','overlap'):run(case,arm,expected=True)
for arm in ('serial','overlap'):
 for fault in ('plan','before_runtime_create','after_runtime_map','after_abi','after_type','after_execution','resident','after_shm','before_publish') + (('compact_setup',) if representation=='compact' else ()):
  run('features',arm,fault=fault,expected=True)
run('features','overlap',fault='after_shm',expected=True,fallback=True)
(out/'functional-results.json').write_text(json.dumps(results,indent=2));print('ALL',len(results),'FUNCTIONAL CHECKS PASSED',flush=True)
