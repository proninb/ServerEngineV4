"""Medium-integrity unsandboxed PUBLISH; elevated helper records only own ETW."""
from pathlib import Path
import csv,ctypes,hashlib,json,os,shutil,statistics,subprocess,sys,time
repo=Path(__file__).resolve().parents[2]; out=Path(sys.argv[1]).resolve()
assert out.is_relative_to(repo/'build')
sys.path.insert(0,str(repo/'experiments/source-cpu-sampling-02'))
from symbols import verify
capture=out/'capture'; frozen=out/'frozen'; frozen.mkdir(exist_ok=False)
source=out/'build/Release/ServerEngineV4PublishBenchmark.exe'
for p in (source,source.with_suffix('.pdb')):shutil.copy2(p,frozen/p.name)
exe=frozen/source.name
identity=verify(exe,exe.with_suffix('.pdb'))
project=repo/'build/parallel-local-source-10-c9/ab-01/fixture/project.json'
fixture=project.parent
artifact=fixture/'.serverengine/project.json'
gold=dict(compiled_bin='b30bdd279af2472096243b99360d02ac70e6337af556c1a9f33739ea5fbafb67',
          runtime_bin='a0281b3bb48103a7cd339cccd072d24f3524db9932bc67b58a714906996b5ee6')
gold={k.replace('_','.'):v for k,v in gold.items()}

def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
inputs={p.relative_to(fixture).as_posix():sha(p) for p in sorted(fixture.rglob('*')) if p.is_file() and '.serverengine' not in p.relative_to(fixture).parts}
(capture/'input-manifest.json').write_text(json.dumps(inputs,indent=2))
rows=[]; seq=0; owned=False
env={k:v for k,v in os.environ.items() if not k.startswith(('CW_','PLS','FAST_ACQUIRE_','PUBLISH_PIPELINE_','SOURCE_STRING_BATCH_'))}
manifest=dict(symbols=identity,exe=str(exe),project=str(project),blocks=4,repeats=8,
    launch='unsandboxed medium-integrity benchmark; separate elevated WPR recorder',
    benchmark_admin=bool(ctypes.windll.shell32.IsUserAnAdmin()),
    recorder=json.loads((capture/'recorder-ready.json').read_text(encoding='utf-8-sig')),
    status='running',golden_hashes=gold)
(frozen/'build-snapshot.json').write_text(json.dumps(dict(symbols=identity,
    source_manifest=json.loads((out/'source-manifest.json').read_text()),snapshot=json.loads((out/'snapshot.json').read_text())),indent=2))

def save():
    (capture/'manifest.json').write_text(json.dumps(manifest,indent=2))
    if rows:
        with (capture/'measurements.csv').open('w',newline='') as f:
            w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)

def recorder(action,block):
    global seq
    seq+=1
    request=dict(seq=seq,action=action,block=block)
    (capture/'request.json').write_text(json.dumps(request))
    deadline=time.monotonic()+150
    while time.monotonic()<deadline:
        if (capture/'recorder-error.json').exists():raise RuntimeError((capture/'recorder-error.json').read_text())
        try:
            response=json.loads((capture/'response.json').read_text(encoding='utf-8-sig'))
            if response['seq']==seq:
                assert response['exit_code']==0,response
                return response
        except (FileNotFoundError,json.JSONDecodeError):pass
        time.sleep(.1)
    raise TimeoutError((action,block))

def run(block,mode,repeat):
    before=time.perf_counter_ns()
    p=subprocess.Popen([str(exe),'publish-timeline',str(project),'24027'],env=env,cwd=repo,
                       stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    stdout,stderr=p.communicate(timeout=120)
    after=time.perf_counter_ns()
    stem=capture/f'b{block}-{mode}-r{repeat}'
    stem.with_suffix('.stdout.txt').write_text(stdout);stem.with_suffix('.stderr.txt').write_text(stderr)
    assert p.returncode==0,(block,mode,repeat,stderr)
    values=dict(x.split('=',1) for x in stdout.splitlines()[0].split(',') if '=' in x)
    assert (values['types'],values['objects'],values['links'])==('24027','137584','367144')
    assert values['phase_only']=='1'
    assert all(sha(artifact/n)==h for n,h in gold.items())
    fields=('total_ms','source_ms','header_ms','lexical_ms','semantic_ms','persistence_ms','resident_ms','transient_release_ms')
    rows.append(dict(block=block,capture=mode,repeat=repeat,pid=p.pid,qpc_start_ns=before,qpc_end_ns=after,
                     **{k:values[k] for k in fields}))
    save();print(block,mode,repeat,p.pid,values['total_ms'],flush=True)

try:
    save()
    run(-1,'warmup',0);run(-1,'warmup',1)
    for block in range(4):
        for mode in (('control','sampled') if block%2==0 else ('sampled','control')):
            if mode=='sampled':recorder('start',block);owned=True
            try:
                for repeat in range(8):run(block,mode,repeat)
            finally:
                if owned:recorder('stop',block);owned=False
    recorder('finish',0)
    assert verify(exe,exe.with_suffix('.pdb'))==identity
    assert all(sha(fixture/p)==h for p,h in inputs.items())
    assert all(sha(out/'source'/p)==h for p,h in json.loads((out/'source-manifest.json').read_text()).items())
    deltas={}
    for key in ('total_ms','source_ms','header_ms','lexical_ms','semantic_ms','persistence_ms','resident_ms'):
        differences=[statistics.mean(float(r[key]) for r in rows if r['block']==b and r['capture']=='sampled')-
                     statistics.mean(float(r[key]) for r in rows if r['block']==b and r['capture']=='control') for b in range(4)]
        deltas[key]=dict(paired_mean_ms=statistics.mean(differences),block_deltas_ms=differences)
    manifest.update(status='captured_not_analyzed',observer_deltas=deltas,inputs_unchanged=True,
                    symbols_unchanged=True,sources_unchanged=True)
    save()
except BaseException as error:
    manifest.update(status='failed',error=str(error));save()
    if owned:
        try:recorder('stop',0)
        except BaseException:pass
    raise
