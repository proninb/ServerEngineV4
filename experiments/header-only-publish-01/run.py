"""Compare isolated full/header-only fixtures using the same production executable."""
import collections
import csv
import hashlib
import json
import pathlib
import shutil
import statistics
import subprocess
import sys

repo = pathlib.Path(__file__).resolve().parents[2]
fixture, exe, output = (pathlib.Path(x).resolve() for x in sys.argv[1:4])
assert output.is_relative_to(repo / 'build')
output.mkdir(parents=True, exist_ok=False)
original = json.loads((fixture / 'project.json').read_text())
counts = collections.Counter()

def prune(nodes):
    kept = []
    for node in nodes:
        counts[node.get('type')] += 1
        if node.get('type') == 'source':
            continue
        node = dict(node)
        if 'children' in node:
            node['children'] = prune(node['children'])
        kept.append(node)
    return kept

header = dict(original)
header['project'] = prune(original['project'])
for mode, config in [('full', original), ('headers', header)]:
    target = output / mode
    target.mkdir()
    # Copy only inputs, never existing compiled/runtime/SHM artifacts.
    for folder in ('Types', 'Books'):
        if folder == 'Books' and mode == 'headers':
            continue
        shutil.copytree(fixture / folder, target / folder)
    (target / 'project.json').write_text(json.dumps(config, indent=2))

sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
manifest = dict(executable=str(exe), executable_sha256=sha(exe),
                original_manifest_sha256=sha(fixture / 'project.json'),
                input_nodes=dict(counts), warmup_pairs=1, measured_pairs=8,
                note='Header-only input excludes sources; not byte-equivalent to full project.')
(output / 'manifest.json').write_text(json.dumps(manifest, indent=2))
rows = []
hashes = {}
for pair in range(-1, 8):
    for mode in (('full', 'headers') if pair % 2 else ('headers', 'full')):
        target = output / mode
        run = subprocess.run([str(exe), 'publish-timeline', str(target / 'project.json'), '24027'], capture_output=True, text=True)
        (output / f'{pair}-{mode}.stdout.txt').write_text(run.stdout)
        (output / f'{pair}-{mode}.stderr.txt').write_text(run.stderr)
        assert run.returncode == 0, run.stdout + run.stderr
        line = next(line for line in run.stdout.splitlines() if line.startswith('mode='))
        row = dict(item.split('=', 1) for item in line.split(','))
        row.update(pair=pair, fixture=mode)
        assert row['types'] == '24027' and row['header_v2_selected'] == '1'
        artifacts = {p.name: sha(p) for p in target.rglob('*.bin') if p.name in ('compiled.bin', 'runtime.bin')}
        assert set(artifacts) == {'compiled.bin', 'runtime.bin'}
        if mode in hashes:
            assert artifacts == hashes[mode], (mode, artifacts, hashes[mode])
        hashes[mode] = artifacts
        rows.append(row)
        print(pair, mode, row['total_ms'], row['header_ms'], row['source_ms'], flush=True)
        with (output / 'measurements.csv').open('w', newline='') as f:
            w = csv.DictWriter(f, fieldnames=rows[0]); w.writeheader(); w.writerows(rows)
summary = {}
for mode in ('full', 'headers'):
    rr = [r for r in rows if r['fixture'] == mode and r['pair'] >= 0]
    summary[mode] = {k: statistics.median(float(r[k]) for r in rr) for k in rr[0] if k.endswith('_ms')}
    summary[mode]['counts'] = {k: rr[0][k] for k in ('types', 'members', 'objects', 'links', 'strings', 'identities')}
summary['artifact_hashes'] = hashes
(output / 'summary.json').write_text(json.dumps(summary, indent=2))
print(json.dumps(summary, indent=2))
