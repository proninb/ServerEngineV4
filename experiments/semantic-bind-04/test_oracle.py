"""Independent V2 oracle, final result lifetimes and actual request teardown."""
import ast
import json
import pathlib
import subprocess
import sys
import tempfile

candidate, legacy, allocations, parent = map(pathlib.Path, sys.argv[1:])
tree = ast.parse((pathlib.Path(__file__).parent.parent / 'semantic-bind-01/test_oracle.py').read_text())
cases = next(ast.literal_eval(n.value) for n in tree.body if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'cases' for t in n.targets))
cases += [(['struct A { const int (*p)[2][3]; int &r; int * const z; };'], True),
          (['struct B { Missing a, b; };'], False)]
parent.mkdir(parents=True, exist_ok=True)
checks = 0
with tempfile.TemporaryDirectory(dir=parent) as folder:
    for index, (sources, accepted) in enumerate(cases):
        root = pathlib.Path(folder) / str(index)
        root.mkdir()
        project = {'version': 1, 'name': 'bound04', 'project': [], 'preprocessor': {'predefines': []}}
        for i, text in enumerate(sources):
            name = f'{i}.h'
            (root / name).write_text(text)
            project['project'].append({'name': name, 'type': 'header', 'path': name})
        path = root / 'project.json'
        path.write_text(json.dumps(project))
        for workers in (1, 4):
            rows = []
            for exe in (candidate, legacy):
                p = subprocess.run([str(exe), str(path), 'combined', str(workers)], capture_output=True, text=True)
                assert p.returncode == 0, (index, exe, p.stdout, p.stderr)
                row = dict(f.split('=', 1) for f in p.stdout.strip().split(','))
                assert row['validation'] == 'passed' and row['oracle_accepted'] == str(int(accepted))
                rows.append(row)
                checks += 1
            for key in ('requests', 'linear_checksum', 'shuffled_checksum'):
                assert rows[0][key] == rows[1][key], (index, key, rows)
        if accepted:
            p = subprocess.run([str(allocations), '--audit', str(path), '4'], capture_output=True, text=True)
            assert p.returncode == 0, (index, p.stdout, p.stderr)
            row = dict(f.split('=', 1) for f in p.stdout.strip().split(','))
            assert row['requests_at_consumer'] == '0' and row['final_live_bytes'] == '0'
            checks += 1
print(f'oracle_and_lifetime_checks={checks},validation=passed')
