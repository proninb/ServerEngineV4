"""Full PUBLISH pairs, 24 Object workers, optional separate parallel pretouch. SHM audits are separate."""
import argparse
import csv
import hashlib
import json
import os
import pathlib
import random
import shutil
import statistics
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('output', type=pathlib.Path)
parser.add_argument('--audit', action='store_true')
parser.add_argument('--pairs', type=int, default=20)
parser.add_argument('--caps', type=int, nargs='+', default=[1])
parser.add_argument('--baseline', type=pathlib.Path)
args = parser.parse_args()
assert 10 <= args.pairs <= 50 and args.caps == [1]
repo = pathlib.Path(__file__).resolve().parents[2]
exe = repo / 'build/runtime-first-touch-01/Release/RuntimeFirstTouch01.exe'
project = repo / 'build/publish-subsecond-01/fixture/project.json'
output = args.output.resolve()
assert output.is_relative_to(repo / 'build')
output.mkdir(exist_ok=False)
frozen = output / exe.name
shutil.copy2(exe, frozen)
baseline = frozen
if args.baseline:
    baseline = output / 'baseline.exe'
    shutil.copy2(args.baseline.resolve(), baseline)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
golden = {'compiled.bin': 'a49506c8df1a999d68624f3a3fd0c317516015b7375deb992ced99736b75522d',
          'runtime.bin': 'a0281b3bb48103a7cd339cccd072d24f3524db9932bc67b58a714906996b5ee6'}
source_files = ['benchmarks/publish_benchmark.cpp', 'server_engine/project/runtime/project_runtime.cpp',
                'server_engine/project/runtime/object_lane_policy.hpp', 'experiments/runtime-first-touch-01/CMakeLists.txt',
                'experiments/runtime-object-lanes-01/shm_audit.hpp', 'experiments/runtime-first-touch-01/run.py']
manifest = {'exe_sha256': sha(frozen), 'control_sha256': sha(baseline), 'project': str(project),
            'golden': golden, 'pairs': args.pairs, 'audit': args.audit, 'caps': args.caps,
            'sources': {p: sha(repo / p) for p in source_files}}
(output / 'manifest.json').write_text(json.dumps(manifest, indent=2))
rows = []


def run(pair, candidate, workers):
    env = dict(os.environ, CW_RUNTIME_OBJECT_WORKERS='24', CW_RUNTIME_OBJECT_PRETOUCH=str(int(workers != 24)))
    env.pop('CW_RUNTIME_SHM_AUDIT', None)
    if args.audit:
        env['CW_RUNTIME_SHM_AUDIT'] = '1'
    selected = baseline if workers == 24 else frozen
    done = subprocess.run([str(selected), 'publish-timeline', str(project), '24027'],
                          env=env, capture_output=True, text=True, check=True)
    stem = f'{pair}-case{candidate}-workers{workers}'
    (output / (stem + '.stdout.txt')).write_text(done.stdout)
    (output / (stem + '.stderr.txt')).write_text(done.stderr)
    row = dict(f.split('=', 1) for f in next(l for l in done.stdout.splitlines() if l.startswith('mode=')).split(','))
    assert row['types'] == '24027' and row['members'] == '727868' and row['phase_only'] == '1'
    if selected == frozen:
        assert int(row['object_lanes']) == 24, row
        assert (int(row['pretouch_bytes']) > 0) == (workers != 24), row
    for name, expected in golden.items():
        assert sha(project.parent / '.serverengine' / project.name / name) == expected, name
    row.update(pair=pair, case=candidate, workers=workers)
    rows.append(row)
    with (output / 'measurements.csv').open('w', newline='') as stream:
        keys = list(dict.fromkeys(k for row in rows for k in row))
        writer = csv.DictWriter(stream, fieldnames=keys)
        writer.writeheader()
        writer.writerows(rows)
    print(pair, candidate, workers, row['total_ms'], row.get('object_phase_ms'), flush=True)


if args.audit:
    assert not args.baseline
    for repeat in range(2):
        for cap in (24, 1):
            run(repeat, cap, cap)
    assert len({r['shm_audit_sha256'] for r in rows}) == 1, 'Resident SHM mismatch'
    assert len({r['shm_audit_bytes'] for r in rows}) == 1
    (output / 'summary.json').write_text(json.dumps({'resident_sha256': rows[0]['shm_audit_sha256'],
        'resident_bytes': rows[0]['shm_audit_bytes'], 'calls': len(rows), 'validation': 'passed'}, indent=2))
else:
    for pair in range(-2, args.pairs):
        order = args.caps[pair % len(args.caps):] + args.caps[:pair % len(args.caps)]
        for index, cap in enumerate(order):
            for workers in ((24, cap) if (pair + index) % 2 == 0 else (cap, 24)):
                run(pair, cap, workers)
    summary = {}
    for cap in args.caps:
        data = [r for r in rows if r['case'] == cap and r['pair'] >= 0]
        summary[cap] = {}
        for metric in ('total_ms', 'resident_ms', 'object_phase_ms', 'pretouch_ms', 'peak_ws_bytes'):
            if not all(metric in row for row in data):
                continue
            differences = []
            for pair in range(args.pairs):
                pair_rows = {r['workers']: r for r in data if r['pair'] == pair}
                differences.append(float(pair_rows[24][metric]) - float(pair_rows[cap][metric]))
            rng = random.Random(20261023)
            estimates = sorted(statistics.mean(rng.choices(differences, k=len(differences))) for _ in range(10000))
            summary[cap][metric] = {
                'reference_median': statistics.median(float(r[metric]) for r in data if r['workers'] == 24),
                'candidate_median': statistics.median(float(r[metric]) for r in data if r['workers'] == cap),
                'paired_gain_median': statistics.median(differences), 'paired_gain_mean': statistics.mean(differences),
                'bootstrap_mean_95_ci': [estimates[249], estimates[9749]],
                'candidate_faster_pairs': sum(d > 0 for d in differences), 'differences': differences,
            }
    (output / 'summary.json').write_text(json.dumps(summary, indent=2))
    print(json.dumps(summary, indent=2))
assert sha(frozen) == manifest['exe_sha256'] and sha(baseline) == manifest['control_sha256']
