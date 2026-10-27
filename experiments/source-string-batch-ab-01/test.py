"""Whole-artifact/SHM/diagnostic parity; separate injected Graph-failure checks."""
from pathlib import Path
import hashlib,json,os,subprocess,sys
repo=Path(__file__).resolve().parents[2]
out=Path(sys.argv[1]).resolve()
cases,logs=out/'cases',out/'functional-logs'
cases.mkdir(exist_ok=True); logs.mkdir(exist_ok=True)
base={k:v for k,v in os.environ.items() if not k.startswith(('CW_','PLS','FAST_ACQUIRE_','PUBLISH_PIPELINE_','SOURCE_STRING_BATCH_'))}
positive={
 'inherited':('struct Base { char text[8]; }; struct Text : Base {}; struct Node { Text nested; };',
    ['Node a; a.nested.text = "longer"; a.nested.text = "x";'],3,1),
 'nested_arrays':('struct Inner { char text[8]; }; struct Root { Inner items[2]; };',
    ['Root a; a.items[0].text = "abc"; a.items[1].text = "xy"; a.items[0].text = "";'],2,1),
 'padding':('struct Inner { char large[128]; }; struct Node { char small[8]; Inner nested; };',
    ['Node a; a.nested.large = "abcdefghijklmnopqrstuvwxyz"; a.small = "B"; a.nested.large = ""; a.small = "C" "D";'],2,2),
 'escapes':('struct Node { char text[32]; };',
    [r'Node a; a.text = "A\n\x42\103\000Z";'],1,0),
 'maximum_literal':('struct Node { char text[8]; };',['Node a; a.text = "1234567";'],1,0),
 'mixed_writes':('struct Node { char text[8]; };',
    ['Node a; a.text[2] = 9; a.text = "hello"; a.text[3] = 7; a.text = "z";'],1,3),
 'references':('struct Text { char text[8]; }; struct Node { Text own; Text& ref; };',
    ['Node a; Node b; Node c; a.ref = b.own; a.ref.text = "AB"; a.ref = c.own; a.ref.text = "Z"; b.own.text = "Q";'],2,None),
 'reference_chain':('struct Node { char text[8]; Node& ref; };',
    ['Node a; Node b; Node c; a.ref = b; b.ref = c; a.ref.ref.text = "abc"; a.ref.ref.text = "z";'],1,1),
 'multiple_roots':('struct Node { char text[8]; };',
    ['Node a; a.text = "hello";','a.text = "x"; a.text[3] = 9;'],1,2),
 'fault':('struct Node { char text[32]; };',['Node a; a.text = "abc";'],1,0),
 'unbound_reference':('struct Text { char text[8]; }; struct Node { Text& ref; };',
    ['Node a; a.ref.text = "abc";'],2,0),
}
negative={
 'overflow':('struct Node { char text[8]; };',['Node a; a.text = "12345678";'],1,None),
 'wide':('struct Node { char text[8]; };',['Node a; a.text = L"abc";'],1,None),
 'bad_escape':('struct Node { char text[8]; };',[r'Node a; a.text = "\x100";'],1,None),
 'non_char':('struct Node { int text[8]; };',['Node a; a.text = "abc";'],1,None),
 'syntax':('struct Node { char text[8]; };',['Node a; a.text = "abc"'],1,None),
 'reference_type_error':('struct Small { char text[8]; }; struct Large { char text[16]; }; struct Node { Small& ref; Large own; };',
    ['Node a; a.ref = a.own; a.ref.text = "x";'],3,None),
}

def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

def fields(text):
    result={}
    for line in text.splitlines():
        for p in line.split(','):
            if '=' not in p:continue
            k,v=p.split('=',1)
            try:result[k]=float(v)
            except ValueError:result[k]=v
    return result

for name,(header,sources,count,warnings) in (positive|negative).items():
    root=cases/name; root.mkdir(exist_ok=True)
    (root/'types.hpp').write_text(header+'\n')
    project=[dict(name='types',type='header',path='types.hpp')]
    for i,source in enumerate(sources):
        filename=f'source{i}.cpp'; (root/filename).write_text(source+'\n')
        project.append(dict(name=f'source{i}',type='source',path=filename))
    (root/'project.json').write_text(json.dumps(dict(version=1,name=name,project=project,preprocessor=dict(predefines=[]))))

results=[]; gold={}
for case,(_,_,count,warnings) in (positive|negative).items():
    for mode in ('scalar','batch'):
        artifact=out/'functional-artifacts'/case/mode
        env=dict(base,SOURCE_STRING_BATCH_01_MODE=mode,SOURCE_STRING_BATCH_01_DIAGNOSTICS='1',FAST_ACQUIRE_02_ARTIFACT_ROOT=str(artifact),CW_RUNTIME_SHM_AUDIT='1')
        for operation in (('publish','audit','load') if case in positive else ('publish',)):
            tag=f'{case}-{mode}-{operation}'
            r=subprocess.run([str(out/'build/Release/ServerEngineV4PublishBenchmark.exe'),operation,str(cases/case/'project.json'),str(count)],env=env,capture_output=True,text=True,timeout=90)
            (logs/(tag+'.stdout')).write_text(r.stdout); (logs/(tag+'.stderr')).write_text(r.stderr)
            if case in positive:
                assert r.returncode==0,(tag,r.stderr)
                f=fields(r.stdout)
                signature=dict(compiled=sha(artifact/'compiled.bin'),runtime=sha(artifact/'runtime.bin'),shm=f['shm_audit_sha256'],diagnostics=r.stderr)
                if operation=='publish' and warnings is not None:
                    assert r.stderr.count('warning:')==warnings,(tag,r.stderr,warnings)
            else:
                assert r.returncode!=0,(tag,r.stdout)
                signature=dict(returncode=r.returncode,diagnostics=r.stderr)
                assert not any((artifact/n).exists() for n in ('compiled.bin','runtime.bin')),tag
            key=(case,operation)
            if mode=='scalar':gold[key]=signature
            else:assert signature==gold[key],(tag,signature,gold[key])
            results.append(dict(tag=tag,signature=signature)); print('PASS',tag,flush=True)

for mode,fault in (('scalar','mid'),('batch','mid'),('batch','reserve')):
    tag=f'fault-{mode}-{fault}'
    artifact=out/'failure-artifacts'/mode/fault
    env=dict(base,SOURCE_STRING_BATCH_01_MODE=mode,SOURCE_STRING_BATCH_01_FAIL=fault,
             FAST_ACQUIRE_02_ARTIFACT_ROOT=str(artifact))
    r=subprocess.run([str(out/'build/Release/ServerEngineV4StringBatchAudit.exe'),'publish',str(cases/'fault/project.json'),'1'],env=env,capture_output=True,text=True,timeout=90)
    (logs/(tag+'.stdout')).write_text(r.stdout); (logs/(tag+'.stderr')).write_text(r.stderr)
    f=fields(r.stdout)
    assert r.returncode==0 and all(f.get(k)==1 for k in ('expected_failure','unpublished','artifacts_removed','shm_released','fault_fired')),(tag,r.stdout,r.stderr)
    if fault=='mid':assert f['string_upserts']==17,(tag,f)
    results.append(dict(tag=tag,fields=f)); print('PASS',tag,flush=True)
(out/'functional-results.json').write_text(json.dumps(results,indent=2))
print('ALL',len(results),'FUNCTIONAL OPERATIONS PASSED',flush=True)
