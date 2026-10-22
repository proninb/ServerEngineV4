"""Independent V2 plus exact SB04/chunk records, including negative cases."""
import ast
import json
import pathlib
import subprocess
import sys
import tempfile

exe, parent = (pathlib.Path(x).resolve() for x in sys.argv[1:])
tree = ast.parse((pathlib.Path(__file__).parent.parent / 'semantic-bind-01/test_oracle.py').read_text())
cases = next(ast.literal_eval(n.value) for n in tree.body if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'cases' for t in n.targets))
cases += [(['namespace Empty {} namespace N { typedef int T; } namespace N { struct A { T value; }; }'], True),
          (['namespace N { struct A { T value; }; typedef int T; }'], False),
          (['namespace N { struct A; } namespace N { struct B { A value; }; struct A {}; }'], False)]
parent.mkdir(parents=True, exist_ok=True)
checks = 0
with tempfile.TemporaryDirectory(dir=parent) as folder:
    for index, (sources, accepted) in enumerate(cases):
        root = pathlib.Path(folder) / str(index)
        root.mkdir()
        obj = {'version': 1, 'name': 'chunk02', 'project': [], 'preprocessor': {'predefines': []}}
        for i, text in enumerate(sources):
            name = f'{i}.h'
            (root / name).write_text(text)
            obj['project'].append({'name': name, 'type': 'header', 'path': name})
        path = root / 'project.json'
        path.write_text(json.dumps(obj))
        for workers in (1, 4):
            for target in (1, 32, 8192):
                p = subprocess.run([str(exe), str(path), str(workers), str(target), '1'], capture_output=True, text=True)
                assert p.returncode == 0, (index, workers, target, p.stdout, p.stderr)
                for line in p.stdout.splitlines():
                    row = dict(field.split('=', 1) for field in line.split(','))
                    assert row['oracle_accepted'] == str(int(accepted)) and row['validation'] == 'passed'
                checks += 1
print(f'independent_oracle_cases={checks},validation=passed')
