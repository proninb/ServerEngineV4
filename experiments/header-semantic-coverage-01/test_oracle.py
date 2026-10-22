"""Constructor scalar slice. Gaps are asserted as partial, never as parity."""
import json
import pathlib
import subprocess
import sys
import tempfile

exe, parent = (pathlib.Path(x).resolve() for x in sys.argv[1:])
cases = [
    (['struct A { int x; A():x(3){} };'], True, False),
    (['struct A { A(){x=2;x=4;} int x; };'], True, False),
    (['struct A { int x; A(){x=-3;} };'], True, False),
    (['struct A { float x; A():x(-0.0){} };'], True, False),
    (['struct A { double x; A():x(1.25){} };'], True, False),
    (['struct A { bool x; A():x(false){} };'], True, False),
    (['struct A { bool x; A():x(0){} };'], True, False),
    (['struct A { bool x; A():x(+true){} };'], True, False),
    (['struct A { int* x; A():x(nullptr){} };'], True, False),
    (['struct A { int* x; A():x(-0){} };'], True, False),
    (['struct A { int x[2]; A():x{}{} };'], True, False),
    (['struct A { int x[2]; A():x(0){} };'], False, False),
    (['struct A { int x; A() noexcept {} };'], True, False),
    (['struct A { int x; A() = default; };'], True, False),
    (['struct A { int x; A(); };'], True, False),
    (['struct A { int* x; A():x(1){} };'], False, False),
    (['struct A { int x; A():x(1.5){} };'], False, False),
    (['struct A { int x; A(){x=-false;} };'], False, False),
    (['struct A { int x; A(){x=-true;} };'], False, False),
    (['struct A { int x; A(){x=1+2;} };'], False, False),
    (['struct A { int x; A(int y){} };'], False, False),
    (['struct A { virtual A(){} };'], False, False),
    (['struct A { A(){} A(int y){} };'], False, False),
    (['struct A { A(){} A(){} };'], False, False),
    (['struct A { A():missing(1){} int x; };'], False, False),
    (['struct A { A():x(1){} Missing y; int x; };'], False, False),
    (['struct A { A(){x=1.5;x=2;} int x; };'], True, False),
    (['struct A { int x; A(){x=1.5;x=2;} };'], False, False),
    (['namespace N { typedef int T; struct A { A():x(1){} T x; }; }'], True, False),
    (['struct A { int x; A():x(1){} };', 'struct A { int x; A():x(1){} };'], True, False),
    (['struct A { int x; A():x(1){} };', 'struct A { int x; A():x(2){} };'], False, False),
    (['struct A { bool x; A():x(false){} };', 'struct A { bool x; A():x(0){} };'], False, False),
    # Explicitly incomplete categories: assert visible gaps.
    (['struct A { int x; int& r; A():r(x){} };'], True, True),
    (['struct B { int x; }; struct A { B b; A(){b.x=2;} };'], True, True),
    (['struct A { int x=2; A(){x=3;} };'], True, True),
]
parent.mkdir(parents=True, exist_ok=True)
checks = 0
with tempfile.TemporaryDirectory(dir=parent) as folder:
    for index, (sources, accepted, partial) in enumerate(cases):
        root = pathlib.Path(folder) / str(index)
        root.mkdir()
        obj = {'version': 1, 'name': 'semantic_coverage', 'project': [], 'preprocessor': {'predefines': []}}
        for i, source in enumerate(sources):
            name = f'{i}.h'
            (root / name).write_text(source)
            obj['project'].append({'name': name, 'type': 'header', 'path': name})
        path = root / 'project.json'
        path.write_text(json.dumps(obj))
        for workers in (1, 4, 16):
            for target in (1, 32768):
                p = subprocess.run([str(exe), str(path), str(workers), str(target), '1'], capture_output=True, text=True)
                assert p.returncode == 0, (index, workers, target, sources, p.stdout, p.stderr)
                for line in p.stdout.splitlines():
                    row = dict(field.split('=', 1) for field in line.split(','))
                    assert row['oracle_accepted'] == str(int(accepted)) and row['validation'] == 'passed'
                    if row['variant'] == 'constructors':
                        assert (int(row['gap_definitions']) > 0) == partial, (index, row)
                        assert row['category_status'] == ('partial' if partial else ('verified_values' if accepted else 'verified_diagnostic'))
                checks += 1
print(f'constructor_oracle_cases={checks},validation=passed')
