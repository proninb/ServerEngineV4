"""Paired native full PUBLISH, same executable, eager/lazy state only."""
import csv
import argparse
import hashlib
import json
import os
import pathlib
import random
import shutil
import statistics
import subprocess
import sys

p = argparse.ArgumentParser()
p.add_argument('executable', type=pathlib.Path)
p.add_argument('project', type=pathlib.Path)
p.add_argument('output', type=pathlib.Path)
p.add_argument('--baseline', type=pathlib.Path, help='Optional original eager executable bridge')
p.add_argument('--pairs', type=int, default=20)
p.add_argument('--control-env', default='CW_HEADER_STATE_EAGER', choices=('CW_HEADER_STATE_EAGER','CW_HEADER_CURSOR_REFERENCE','CW_GRAPH_MEMBER_INDEX_REFERENCE','CW_GRAPH_LINK_INDEX_REFERENCE','CW_STRING_INTERN_SLOT_REFERENCE','CW_PUBLISH_SHA256_REFERENCE','CW_RUNTIME_RANGES_REFERENCE','CW_EARLY_RELEASE_REFERENCE'))
args = p.parse_args()
assert 10 <= args.pairs <= 50
exe, project, output = (x.resolve() for x in (args.executable,args.project,args.output))
repo = pathlib.Path(__file__).resolve().parents[2]
assert output.is_relative_to(repo / 'build') and project.is_relative_to(repo / 'build')
output.mkdir(parents=True, exist_ok=False)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
frozen = output / exe.name
shutil.copy2(exe, frozen)
if not args.baseline:
    assert (args.control_env+'\x00').encode() in frozen.read_bytes(), 'build benchmark with the matching A/B option first'
control = frozen
if args.baseline:
    control = output / 'baseline.exe'
    shutil.copy2(args.baseline.resolve(), control)
artifacts = project.parent / '.serverengine' / project.name
golden = {name: sha(artifacts / name) for name in ('compiled.bin', 'runtime.bin')}
manifest = dict(executable_sha256=sha(frozen), project=str(project), project_sha256=sha(project),
                golden=golden, warmup_pairs=2, measured_pairs=args.pairs,
                control='original reference executable' if args.baseline else args.control_env+'=1', candidate=args.control_env+'=0',
                control_executable_sha256=sha(control))
manifest['sources'] = {str(p.relative_to(repo)): sha(p) for p in [
    repo / 'server_engine/project/parser/header_record_index.hpp',
    repo / 'server_engine/project/parser/header_parser_v2.hpp',
    repo / 'server_engine/project/parser/header_parser_v2.cpp',
    repo / 'benchmarks/publish_benchmark.cpp', repo / 'CMakeLists.txt',
    repo / 'server_engine/project/file/file_context.hpp',
    repo / 'server_engine/project/file/file_context.cpp',
    repo / 'server_engine/project/file/file_identity.hpp',
    repo / 'server_engine/project/file/file_identity.cpp',
    repo / 'server_engine/project/project_configuration_manifest.cpp',
    repo / 'server_engine/project/project_full_construction.cpp',
    repo / 'server_engine/project/shm/shm_type_batch.cpp',
    repo / 'server_engine/project/graph/graph.cpp',
    repo / 'server_engine/project/string/string_table.cpp']}
if args.control_env == 'CW_HEADER_CURSOR_REFERENCE':
    for rel in ('server_engine/project/frontend/semantic_cursor_v2.hpp','server_engine/project/frontend/lexical_symbols_v2.hpp'):
        manifest['sources'][rel]=sha(repo/rel)
if args.control_env == 'CW_GRAPH_MEMBER_INDEX_REFERENCE':
    for rel in ('server_engine/project/graph/graph.cpp','server_engine/project/graph/member_index_policy.hpp','experiments/header-graph-index-01/CMakeLists.txt'):
        manifest['sources'][rel]=sha(repo/rel)
(output / 'manifest.json').write_text(json.dumps(manifest, indent=2))
rows = []
for pair in range(-2, args.pairs):
    for mode in (('eager', 'lazy') if pair % 2 == 0 else ('lazy', 'eager')):
        env = dict(os.environ, CW_HEADER_STATE_EAGER='0')
        env[args.control_env] = '1' if mode == 'eager' else '0'
        run = subprocess.run([str(control if mode=='eager' else frozen), 'publish-timeline', str(project), '24027'], env=env, capture_output=True, text=True)
        (output / f'{pair}-{mode}.stdout.txt').write_text(run.stdout)
        (output / f'{pair}-{mode}.stderr.txt').write_text(run.stderr)
        assert run.returncode == 0, run.stdout + run.stderr
        row = dict(f.split('=', 1) for f in next(l for l in run.stdout.splitlines() if l.startswith('mode=')).split(','))
        assert row['types'] == '24027' and row['members'] == '727868' and row['header_v2_selected'] == '1'
        assert {name: sha(artifacts / name) for name in golden} == golden
        row.update(pair=pair, variant=mode); rows.append(row)
        with (output / 'measurements.csv').open('w', newline='') as f:
            w = csv.DictWriter(f, fieldnames=rows[0]); w.writeheader(); w.writerows(rows)
        print(pair, mode, row['total_ms'], row['header_ms'], flush=True)
summary = {}
for mode in ('eager', 'lazy'):
    rr = [r for r in rows if r['variant'] == mode and r['pair'] >= 0]
    summary[mode] = {k: statistics.median(float(r[k]) for r in rr) for k in ('total_ms', 'header_ms', 'header_v2_physical_ms', 'source_ms', 'peak_ws_bytes')}
for metric in ('total_ms', 'header_ms', 'source_ms'):
    differences = []
    for pair in range(args.pairs):
        r = {x['variant']: x for x in rows if x['pair'] == pair}
        differences.append(float(r['eager'][metric]) - float(r['lazy'][metric]))
    rng = random.Random(20261023)
    estimates = sorted(statistics.mean(rng.choices(differences, k=len(differences))) for _ in range(10000))
    summary[metric] = dict(paired_gain_median_ms=statistics.median(differences),
                           paired_gain_mean_ms=statistics.mean(differences),
                           bootstrap_mean_95_ci_ms=[estimates[249], estimates[9749]],
                           candidate_faster_pairs=sum(x > 0 for x in differences), differences_ms=differences)
(output / 'summary.json').write_text(json.dumps(summary, indent=2))
assert sha(exe) == manifest['executable_sha256'], 'executable changed during measurement'
print(json.dumps(summary, indent=2))
