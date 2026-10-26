"""Small end-to-end parity and BUILD/REBUILD checks before timed runs."""
from pathlib import Path
import hashlib, json, os, shutil, subprocess, sys

repo = Path(__file__).resolve().parents[2]
out = Path(sys.argv[1]).resolve()
exe = out / 'build/Release/ServerEngineV4PublishBenchmark.exe'
cases, logs = out / 'cases', out / 'functional-logs'
if not cases.exists():
    shutil.copytree(repo / 'build/publish-pipeline-01-compact/cases', cases)
logs.mkdir(exist_ok=True)
base = {k:v for k,v in os.environ.items()
        if not k.startswith(('FAST_ACQUIRE_', 'CW_', 'PLS', 'PUBLISH_PIPELINE_'))}
positive = dict(features=3, nested=2, late_binding=1, constructor=2, legacy=1)
negative = dict(parse_error=1, reserved=1, reference_cycle=1, extern_mismatch=0)
results, gold = [], {}
legacy = cases / 'legacy'
legacy.mkdir(exist_ok=True)
(legacy/'types.hpp').write_text('struct Node { int value; };\n')
(legacy/'objects.cpp').write_text('Node a; Node b; a.value = 7;\n')
(legacy/'project.json').write_text(json.dumps(dict(version=1, name='legacy', project=[
    dict(name='types', type='header', path='types.hpp'),
    dict(name='objects', type='source', path='objects.cpp')], preprocessor=dict(predefines=[]))))

def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

for case, count in (positive | negative).items():
    for arm in ('reference', 'fast'):
        artifact = out / 'functional-artifacts' / case / arm
        env = dict(base, FAST_ACQUIRE_02_REFERENCE='1' if arm == 'reference' else '0',
                   FAST_ACQUIRE_02_ARTIFACT_ROOT=str(artifact), CW_RUNTIME_SHM_AUDIT='1')
        operations = (('rebuild', 'build', 'publish', 'audit', 'load') if case == 'legacy' else
                      ('publish', 'audit', 'load') if case in positive else ('publish',))
        for operation in operations:
            label = f'{case}-{arm}-{operation}'
            result = subprocess.run([str(exe), operation, str(cases / case / 'project.json'), str(count)],
                                    env=env, capture_output=True, text=True, timeout=90)
            (logs / (label+'.stdout')).write_text(result.stdout)
            (logs / (label+'.stderr')).write_text(result.stderr)
            if case in negative:
                assert result.returncode != 0, label
                signature = (result.returncode, result.stderr)
            else:
                assert result.returncode == 0, (label, result.stderr)
                fields = dict(p.split('=',1) for p in result.stdout.strip().split(',') if '=' in p)
                signature = dict(compiled=sha(artifact/'compiled.bin'), runtime=sha(artifact/'runtime.bin'),
                                 shm=fields['shm_audit_sha256'])
            key = (case, operation)
            if arm == 'reference':
                gold[key] = signature
            else:
                assert signature == gold[key], (label, signature, gold[key])
            results.append(dict(label=label, returncode=result.returncode, signature=signature))
            print('PASS', label, flush=True)
(out/'functional-results.json').write_text(json.dumps(results, indent=2))
print('ALL', len(results), 'CHECKS PASSED', flush=True)
