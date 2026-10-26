"""Alternating same-binary full UnitProXL study; all hashing outside timing."""
from pathlib import Path
import hashlib, json, os, random, shutil, statistics, subprocess, sys

repo = Path(__file__).resolve().parents[2]
out = Path(sys.argv[1]).resolve()
assert out.is_relative_to(repo / 'build')
logs = out / 'timing-logs'
logs.mkdir(exist_ok=False)
exe = out / 'binaries/PublishFastAcquire02.exe'
exe.parent.mkdir()
shutil.copy2(out / 'build/Release/ServerEngineV4PublishBenchmark.exe', exe)

def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

binary_hash = sha(exe)
(out / 'binary.json').write_text(json.dumps(dict(sha256=binary_hash,
    configuration='MSVC Release + IPO; production flags; PGO OFF'), indent=2))
inputs = json.loads((repo / 'build/publish-pipeline-01-compact/input-manifest.json').read_text())
(out / 'input-manifest.json').write_text(json.dumps(inputs, indent=2))
base = {k:v for k,v in os.environ.items()
        if not k.startswith(('FAST_ACQUIRE_', 'CW_', 'PLS', 'PUBLISH_PIPELINE_'))}
samples, checks, gold = [], [], {}

def verify_inputs():
    for name, data in inputs.items():
        assert all(sha(Path(data['root']) / p) == h for p,h in data['manifest'].items()), name

def fields(text):
    result = {}
    for line in text.splitlines():
        for part in line.split(','):
            if '=' not in part:
                continue
            key, value = part.split('=', 1)
            try:
                result[key] = float(value)
            except ValueError:
                result[key] = value
    return result

def run(corpus, arm, operation, tag, audit=False):
    data = inputs[corpus]
    artifact = out / 'artifacts' / corpus / arm
    env = dict(base, FAST_ACQUIRE_02_REFERENCE='1' if arm == 'reference' else '0',
               FAST_ACQUIRE_02_ARTIFACT_ROOT=str(artifact))
    if audit:
        env['CW_RUNTIME_SHM_AUDIT'] = '1'
    label = f'{corpus}-{tag}-{arm}-{operation}'
    result = subprocess.run([str(exe), operation, data['project'], str(data['expected_types'])],
                            env=env, capture_output=True, text=True, timeout=240)
    (logs / (label + '.stdout')).write_text(result.stdout)
    (logs / (label + '.stderr')).write_text(result.stderr)
    assert result.returncode == 0, (label, result.returncode, result.stderr[-4000:])
    row = fields(result.stdout)
    if corpus == 'unitproxl':
        assert (row['types'], row['objects'], row['links']) == (24027, 137584, 367144)
    payload = {name:sha(artifact / name) for name in ('compiled.bin', 'runtime.bin')}
    if corpus not in gold:
        gold[corpus] = payload
    assert gold[corpus] == payload, (label, 'artifact parity', payload)
    if audit:
        key = corpus + '-shm'
        if key not in gold:
            gold[key] = row['shm_audit_sha256']
        assert gold[key] == row['shm_audit_sha256'], (label, 'SHM parity')
    if operation == 'publish-timeline':
        assert row['persistence_flush_enabled'] == 0 and row['compiled_flush_ms'] == 0
    row.update(corpus=corpus, arm=arm, operation=operation, tag=tag)
    print(label, row['total_ms'], 'ms', flush=True)
    return row

verify_inputs()
for corpus in ('synthetic', 'unitproxl'):
    for arm in ('reference', 'fast'):
        for operation in ('publish', 'audit', 'load'):
            checks.append(run(corpus, arm, operation, 'pre', True))
    if corpus == 'unitproxl':
        for i in range(2):
            for arm in (('reference', 'fast') if i % 2 == 0 else ('fast', 'reference')):
                checks.append(run(corpus, arm, 'publish', f'warm-{i}'))
        for series, operation, count in (('native', 'publish', 15), ('profile', 'publish-timeline', 7)):
            for pair in range(count):
                order = ('reference', 'fast') if pair % 2 == 0 else ('fast', 'reference')
                for arm in order:
                    row = run(corpus, arm, operation, f'{series}-{pair:02}')
                    row.update(series=series, pair=pair)
                    samples.append(row)
                (out / 'samples.json').write_text(json.dumps(samples, indent=2))
    for arm in ('fast', 'reference'):
        for operation in ('audit', 'load'):
            checks.append(run(corpus, arm, operation, 'post', True))

rng = random.Random(26026)
summary = {}
for series in ('native', 'profile'):
    rows = [r for r in samples if r['series'] == series]
    arms = {arm: sorted([r for r in rows if r['arm'] == arm], key=lambda r:r['pair'])
            for arm in ('reference', 'fast')}
    medians = {arm: {key:statistics.median(r[key] for r in values)
                    for key in values[0] if all(isinstance(r[key], (int,float)) for r in values)}
               for arm, values in arms.items()}
    gains = [a['total_ms']-b['total_ms'] for a,b in zip(arms['reference'], arms['fast'])]
    boot = sorted(statistics.median(rng.choices(gains, k=len(gains))) for _ in range(20000))
    summary[series] = dict(medians=medians, paired_gain_ms=statistics.median(gains),
        gain_ci95_ms=[boot[500], boot[19499]], fast_wins=sum(g>0 for g in gains), pairs=len(gains))
(out / 'summary.json').write_text(json.dumps(summary, indent=2))
(out / 'checks.json').write_text(json.dumps(checks, indent=2))
(out / 'parity.json').write_text(json.dumps(gold, indent=2))
verify_inputs()
assert sha(exe) == binary_hash
manifest = json.loads((out / 'source-manifest.json').read_text())
assert all(sha(out / 'source' / p) == h for p,h in manifest.items())
(out / 'verified.json').write_text(json.dumps(dict(binary_unchanged=True,
    inputs_unchanged=True, sources_unchanged=True, parity_checks=len(checks), samples=len(samples)), indent=2))
print(json.dumps(summary, indent=2), flush=True)
