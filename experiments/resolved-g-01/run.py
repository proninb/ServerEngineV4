"""Observe inactive-hook overhead separately, then measure resolved API replay."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import random
import statistics
import subprocess

repo = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('output', type=Path)
p.add_argument('--exe', type=Path, default=repo/'build/resolved-g-01/engine/Release/ResolvedGReplay01.exe')
p.add_argument('--baseline', type=Path, default=repo/'build/enum-v4/Release/ServerEngineV4PublishBenchmark.exe')
p.add_argument('--project', type=Path, default=repo/'build/publish-subsecond-01/fixture/project.json')
p.add_argument('--types', type=int, default=24027)
p.add_argument('--pairs', type=int, default=10)
p.add_argument('--replays', type=int, default=15)
args = p.parse_args()
assert 5 <= args.pairs <= 50 and 3 <= args.replays <= 100
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
project = args.project.resolve()
reference = None
rows = []

def parse(line):
    return dict(item.split('=', 1) for item in line.split(',')[1:])

def run(exe, capture, pair, label):
    global reference
    env = dict(os.environ, CW_RESOLVED_G_CAPTURE=str(capture), CW_RESOLVED_G_REPEATS=str(args.replays),
               CW_RESOLVED_G_CORRUPT='0')
    result = subprocess.run([str(exe.resolve()), 'publish-timeline', str(project), str(args.types)],
                            capture_output=True, text=True, env=env)
    stem = f'{pair:03}-{label}'
    (out/(stem+'.stdout')).write_text(result.stdout)
    (out/(stem+'.stderr')).write_text(result.stderr)
    if result.returncode:
        raise RuntimeError(f'{stem}: exit {result.returncode}: {result.stderr[-1500:]}')
    artifact = project.parent/'.serverengine'/project.name
    digest = {name: hashlib.sha256((artifact/name).read_bytes()).hexdigest()
              for name in ('compiled.bin', 'runtime.bin')}
    if reference is None:
        reference = digest
        if project == (repo/'build/publish-subsecond-01/fixture/project.json').resolve():
            assert reference == {
                'compiled.bin':'a49506c8df1a999d68624f3a3fd0c317516015b7375deb992ced99736b75522d',
                'runtime.bin':'a0281b3bb48103a7cd339cccd072d24f3524db9932bc67b58a714906996b5ee6'}
    assert digest == reference, (stem, digest, reference)
    mode = next(line for line in result.stdout.splitlines() if line.startswith('mode='))
    row = dict(item.split('=',1) for item in mode.split(','))
    row.update(pair=pair, label=label)
    assert row['phase_only'] == '1'
    if not capture:
        rows.append(row)
        with (out/'controls.csv').open('w', newline='') as f:
            writer=csv.DictWriter(f, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
        print(pair, label, row['total_ms'], row['source_ms'], flush=True)
    return result

for pair in range(-2,args.pairs):
    order = [('baseline',args.baseline),('inactive',args.exe)]
    if pair % 2: order.reverse()
    for label,exe in order:
        run(exe,0,pair,label)

captured = run(args.exe,1,0,'capture')
replays = [parse(line) for line in captured.stdout.splitlines() if line.startswith('resolved_g01,')]
counts = parse(next(line for line in captured.stdout.splitlines() if line.startswith('resolved_g01_summary,')))
assert len(replays)==args.replays+2 and all(row['parity']=='1' for row in replays)
with (out/'replays.csv').open('w', newline='') as f:
    writer=csv.DictWriter(f,fieldnames=list(replays[0])); writer.writeheader(); writer.writerows(replays)

def interval(data):
    rng=random.Random(241024)
    samples=sorted(statistics.median(rng.choices(data,k=len(data))) for _ in range(10000))
    return [samples[249],samples[9749]]

summary = {'artifacts':reference,'operations':counts,'control_pairs':args.pairs,
           'replay_samples':args.replays,'inactive_hook_overhead':{},'controls':{},'replay':{}}
for metric in ('source_ms','total_ms'):
    values={label:[float(row[metric]) for row in rows if row['pair']>=0 and row['label']==label]
            for label in ('baseline','inactive')}
    summary['controls'][metric]={label:statistics.median(v) for label,v in values.items()}
    differences=[]
    for pair in range(args.pairs):
        pair_rows={row['label']:row for row in rows if row['pair']==pair}
        differences.append(float(pair_rows['inactive'][metric])-float(pair_rows['baseline'][metric]))
    summary['inactive_hook_overhead'][metric]={'paired_median_ms':statistics.median(differences),
        'bootstrap_95_ms':interval(differences),'differences_ms':differences}
measured=[row for row in replays if int(row['repeat'])>=0]
for metric in ('g_ms','prepare_ms','verify_ms','release_ms','final_capacity_bytes'):
    values=[float(row[metric]) for row in measured]
    summary['replay'][metric]={'median':statistics.median(values),'min':min(values),'max':max(values)}
summary['replay_vs_source_ratio']=summary['replay']['g_ms']['median']/summary['controls']['source_ms']['baseline']
summary['limitations']=[
    'Replay includes tape dispatch and resolved Graph API validation/indexing/allocation; it is not exclusive CPU sampling.',
    'Header clone/capacity preparation, capture, parity checking, release and external SHA-256 are outside g_ms.',
    'Capture-mode Source/PUBLISH timers include replay overhead and must never be used as production timings.',
    'Source minus G replay is not an additive decomposition: input layout, allocator and cache states differ.',
    'Only Source mutations are replayed; Header G already exists. No new production algorithm is enabled.']
(out/'summary.json').write_text(json.dumps(summary,indent=2))
print(json.dumps(summary,indent=2),flush=True)
