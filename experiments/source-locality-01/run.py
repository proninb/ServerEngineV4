"""Capture Source locality on isolated fixtures; diagnostic times are not speed evidence."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

repo = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('output', type=Path)
parser.add_argument('--corpora', nargs='+', default=['unitproxl', 'unitproxl-v2', 'abb'])
args = parser.parse_args()
output = args.output.resolve()
assert output.is_relative_to(repo / 'build')
output.mkdir(exist_ok=False)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
executables = {}
for mode in ('SourceLocality01', 'SourceLocality01Control'):
    source = repo / 'build/source-locality-01/Release' / (mode + '.exe')
    target = output / source.name
    shutil.copy2(source, target)
    executables[mode] = target
sources = [
    'benchmarks/publish_benchmark.cpp', 'server_engine/project/parser/source_parser_v2.cpp',
    'experiments/source-locality-01/capture.hpp', 'experiments/source-locality-01/CMakeLists.txt',
    'experiments/source-locality-01/run.py', 'experiments/source-locality-01/prepare.py',
    'scripts/prepare_unitproxl_project.py']
(output / 'manifest.json').write_text(json.dumps({'executables': {p.name: sha(p) for p in executables.values()},
    'sources': {name: sha(repo / name) for name in sources}}, indent=2))
fixtures = {'unitproxl': repo / 'build/publish-subsecond-01/fixture/project.json',
            'unitproxl-v2': repo / 'build/source-locality-01/fixtures/unitproxl-v2/project.json',
            'abb': repo / 'build/source-locality-01/fixtures/abb/project.json'}
results = {}
for name in args.corpora:
    project = fixtures[name]
    directory = output / name
    directory.mkdir()
    base = project.parent / '.serverengine' / project.name
    def invoke(mode, expected, label, trace=None):
        env = dict(os.environ)
        env.pop('CW_SOURCE_LOCALITY_OUTPUT', None)
        if trace:
            env['CW_SOURCE_LOCALITY_OUTPUT'] = str(trace)
        process = subprocess.run([str(executables[mode]), 'publish-timeline', str(project), str(expected)],
                                 env=env, capture_output=True, text=True, errors='replace')
        (directory / (label + '.stdout.txt')).write_text(process.stdout, encoding='utf-8')
        (directory / (label + '.stderr.txt')).write_text(process.stderr, encoding='utf-8')
        return process
    try:
        expected = 24027 if name == 'unitproxl' else 0
        if not expected:
            probe = invoke('SourceLocality01Control', 0, 'probe')
            match = re.search(r'Unexpected type count:\s*(\d+)\s*!=\s*0', probe.stderr)
            if probe.returncode != 3 or not match:
                raise RuntimeError(f'Production probe failed ({probe.returncode}): {probe.stderr[-1600:]}')
            expected = int(match[1])
        control = invoke('SourceLocality01Control', expected, 'control')
        if control.returncode:
            raise RuntimeError(f'Production control failed ({control.returncode}): {control.stderr[-1600:]}')
        row = dict(x.split('=', 1) for x in next(x for x in control.stdout.splitlines() if x.startswith('mode=')).split(','))
        golden = {file: sha(base / file) for file in ('compiled.bin', 'runtime.bin')}
        if name == 'unitproxl':
            assert golden == {'compiled.bin': 'a49506c8df1a999d68624f3a3fd0c317516015b7375deb992ced99736b75522d',
                              'runtime.bin': 'a0281b3bb48103a7cd339cccd072d24f3524db9932bc67b58a714906996b5ee6'}
        for repeat in range(2):
            trace = directory / f'capture-{repeat}'
            audit = invoke('SourceLocality01', expected, f'audit-{repeat}', trace)
            if audit.returncode:
                raise RuntimeError(f'Diagnostic PUBLISH failed ({audit.returncode}): {audit.stderr[-1600:]}')
            assert {file: sha(base / file) for file in golden} == golden, 'Artifact parity failed'
            if repeat:
                assert sha(trace / 'events.csv') == sha(directory / 'capture-0/events.csv'), 'Locality events changed'
        results[name] = {'status': 'passed', 'project': str(project), 'golden': golden,
                         'types': expected, 'members': int(row['members']), 'objects': int(row['objects']),
                         'links': int(row['links']), 'source_roots': int(row['source_root_count']),
                         'event_sha256': sha(directory / 'capture-0/events.csv')}
        print(name, json.dumps(results[name]), flush=True)
    except Exception as error:
        results[name] = {'status': 'failed', 'project': str(project), 'error': str(error)}
        prefix = invoke('SourceLocality01', expected, 'failure-audit', directory / 'failure-prefix')
        results[name]['diagnostic_exit_code'] = prefix.returncode
        results[name]['diagnostic_prefix_only'] = True
        print(name, str(error), flush=True)
    (output / 'results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
