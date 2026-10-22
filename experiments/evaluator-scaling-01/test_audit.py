"""All measurement modes must preserve values and first diagnostic vs SC03/V2."""
import csv
import json
import pathlib
import subprocess
import sys
import tempfile

exe, parent = map(pathlib.Path, sys.argv[1:])
parent.mkdir(parents=True, exist_ok=True)
cases = [
    ['struct A { int a[2]; A(){a[0]=1;} };', 'struct A { int a[2]; A(){a[1]=2;} };'],
    ['struct A { int a[2]; A(){a[0]=1;} };', 'struct A { int a[2]; A(){a[0]=2;} };'],
    ['struct A { int& r; A():r(late){r=nullptr;} int late; }; struct B { int a[2]; B(){a[0]=1;} };'],
    ['struct A { int& r; A():r(late){r=nullptr;} bool late; }; struct B { B(){missing[0]=1;} };'],
]
with tempfile.TemporaryDirectory(dir=parent) as tmp:
    for index, sources in enumerate(cases):
        folder = pathlib.Path(tmp) / str(index)
        folder.mkdir()
        project = {'version': 1, 'name': 'audit', 'project': [], 'preprocessor': {'predefines': []}}
        for i, source in enumerate(sources):
            name = f'{i}.h'
            (folder / name).write_text(source)
            project['project'].append({'name': name, 'type': 'header', 'path': name})
        path = folder / 'project.json'
        path.write_text(json.dumps(project))
        for workers in (1, 16):
            out = folder / f'output-{workers}'
            p = subprocess.run([str(exe), '--audit', str(path), str(workers), '1', '1', str(out)], capture_output=True, text=True)
            assert p.returncode == 0, p.stdout + p.stderr
            rows = list(csv.DictReader((out / 'summary.csv').open()))
            assert {r['mode'] for r in rows} == set('01234')
            assert len({r['checksum'] for r in rows}) == 1
            assert all(float(r['exclusive_ms']) >= 0 for r in rows)
print('audit_mode_cases=8,validation=passed')
