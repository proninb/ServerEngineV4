"""Nested target slice. Gaps are asserted as partial, never as parity."""
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
    (['struct A { int x; int& r; A():r(x){} };'], True, False),
    (['struct B { int x; }; struct A { B b; A(){b.x=2;} };'], True, False),
    (['struct A { int x=2; A(){x=3;} };'], True, True),
]

cases += [
    (['struct A { int x; int& r; A():r(x){} };'], True, False),
    (['struct A { int& r; A():r(x){} int x; };'], True, False),
    (['struct A { A():r(x){} int& r; int x; };'], True, False),
    (['struct A { int x; int& r; A():r(this.x){} };'], True, False),
    (['struct A { int x; int y; int& r; A():r(x){r=y;} };'], True, False),
    (['struct A { int& r; A(){r=a;r=b;} int b; int a; };'], True, False),
    (['struct A { int& r; A():r(late){r=nullptr;} int late; };'], True, False),
    (['struct A { int& r; A():r(late){r=nullptr;} bool late; };'], False, False),
    (['struct A { int& r; A():r(missing){r=nullptr;} };'], False, False),
    (['struct A { int known; int& r; A():r(late){r=known;} int late; };'], True, False),
    (['struct A { int known; int& r; A():r(late){r=known;} bool late; };'], False, False),
    (['struct A { A(){r=missing;r=x;} int& r; int x; };'], True, False),
    (['struct A { int& r; A(){r=missing;} };'], False, False),
    (['struct A { int& r; A(){r=missing;unknown=0;} };'], False, False),
    (['struct A { int& r; A(){r=missing;} Missing value; };'], False, False),
    (['struct A { bool x; int& r; A():r(x){} };'], False, False),
    (['struct A { int& r; A():r(x){} bool x; };'], False, False),
    (['struct A { int x; int r; A():r(x){} };'], False, False),
    (['struct A { int x; int* r; A():r(x){} };'], False, False),
    (['struct A { const int x; const int& r; A():r(x){} };'], True, False),
    (['struct A { int x; const int& r; A():r(x){} };'], False, False),
    (['struct A { int x; int& s; int& r; A():s(x),r(s){} };'], True, False),
    (['struct A { int x; int&& r; A():r(x){} };'], True, False),
    (['struct A { int x[2]; int (&r)[2]; A():r(x){} };'], True, False),
    (['struct A { int x[3]; int (&r)[2]; A():r(x){} };'], False, False),
    (['struct A { int& r; int& q; A():r(x),q(x){} bool x; };'], False, False),
    (['struct A { int x; int& r; A():r(this->x){} };'], False, False),
    (['struct A { int x; int& r; A():r(this.){} };'], False, False),
    (['namespace N { typedef int T; struct A { T x; T& r; A():r(x){} }; }'], True, False),
    (['struct A { int x; int y; int& r; A():r(x){} };', 'struct A { int x; int y; int& r; A():r(y){} };'], False, False),
]

cases += [
    (['struct A { int x; A():r(x){} int r; };'], False, False),
    (['struct A { int x; A():r(x){} bool& r; };'], False, False),
    (['struct A { int x; A():r(x){} Missing bad; bool& r; };'], False, False),
    (['struct A { int x; int& r; A():r(x){} };', 'struct A { int x; int& r; A():r(x){} };'], True, False),
]
seen_stale = seen_queued = seen_pending = seen_superseded = False
cases += [
 (['struct B { int x; }; struct A { A(){b.x=1;b.x=2;} B b; };'],True,False),
 (['struct A { int a[2][3]; A(){a[1][2]=7;} };'],True,False),
 (['struct B { int x; }; struct A { B b[2]; A(){b[1].x=3;} };'],True,False),
 (['struct B { int x; }; struct A { B b; A(){b.missing=3;} };'],False,False),
 (['struct A { int a[2]; A(){a[2]=3;} };'],False,False),
 (['struct A { int a; A(){a[0]=3;} };'],False,False),
 (['struct B { int x; }; struct A { B* b; A(){b.x=3;} };'],False,False),
 (['struct B { int x; }; struct A { B& b; A(){b.x=3;} };'],False,False),
 (['struct B { int& x; }; struct A { B b; A(){b.x=0;} };'],False,False),
 (['struct B { int x; }; struct A { B b; A(){b.x=other;} };'],False,False),
 (['struct A { A(){missing.x=1;} };'],False,False),
 (['struct A { A(){a[3]=1;a[0]=2;} int a[2]; };'],False,False),
 (['struct A { int a[2]; A(){a[-1]=1;} };'],False,False),
 (['struct A { int a[2]; A(){a[1.0]=1;} };'],False,False),
 (['struct A { int a[2]; A(){a[0=1;} };'],False,False),
 (['struct B { int x; }; struct A { B b; A(){b.=1;} };'],False,False),
 (['struct A { int a[2]; A(){a[0]=1;} };','struct A { int a[2]; A(){a[1]=2;} };'],True,False),
 (['struct A { int a[2]; A(){a[0]=1;} };','struct A { int a[2]; A(){a[0]=2;} };'],False,False),
]

cases += [
 (['struct B { int x; }; struct A { B b; A(){b.x=1;b.x=2;} };'],True,False),
 (['struct B { int x[2]; }; struct A { B b[2]; A(){b[1].x[0]=2;} };'],True,False),
 (['typedef int T; struct B { T x; }; struct A { B b; A(){b.x=2;} };'],True,False),
 (['struct B { const int x; }; struct A { B b; A(){b.x=2;} };'],False,False),
 (['struct B { int* x; }; struct A { B b; A(){b.x=0;} };'],False,False),
 (['struct B { int x[2]; }; struct A { B b; A(){b.x=0;} };'],False,False),
 (['struct B { int x; }; struct A { const B b; A(){b.x=2;} };'],False,False),
 (['struct Base { int x; }; struct B: Base {}; struct A { B b; A(){b.x=2;} };'],False,False),
 (['struct A { A(){missing[0]=1;r=absent;} int& r; };'],False,False),
 (['struct A { A(){missing[0]=1;other=1;} };'],False,False),
 (['struct A { int a[2]; A(){a[3]=1;a[0=2;} };'],False,False),
 (['struct A { int a[2]; A(){a[0]=1;} };','struct A { int a[2]; A(){a[0]=1;} };'],True,False),
 (['namespace N { struct B { int x; }; struct A { B b; A(){b.x=2;} }; }'],True,False),
 (['struct A { int a[2]; A(){a'+('[0]'*257)+'=1;} };'],False,False),
]

cases += [
 (['struct A { int a[2]; A(){a[0]=1;} };','struct A { int a[2]; A(){a[0]=2;} }; struct B { B(){missing[0]=1;} };'],False,False),
 (['struct A { int x; A(){x=1;} };','struct A { int x; A(){x=2;} }; struct B { int& r; B():r(absent){} };'],False,False),
 (['struct A { A(){missing[0]=1;} }; struct B { B(){other=1;} };'],False,False),
 (['struct A { int& r; A():r(late){r=nullptr;} int late; };','struct A { int& r; A():r(late){r=nullptr;} int late; };'],True,False),
 (['struct Inner { int x; }; struct A { Inner b; A(){b.x=1;} };','struct A { Inner b; A(){b.x=1;} };'],True,False),
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
                    if row['variant'] == 'isolated':
                        assert (int(row['gap_definitions']) > 0) == partial, (index, row)
                        seen_stale |= int(row['stale_references']) > 0
                        seen_queued |= int(row['queued_references']) > 0
                        seen_pending |= int(row['pending_reference_targets']) > 0
                        seen_superseded |= int(row['superseded_reference_targets']) > 0
                        assert row['category_status'] == ('partial' if partial else ('verified_values' if accepted else 'verified_diagnostic'))
                checks += 1
assert seen_stale and seen_queued and seen_pending and seen_superseded
print(f'constructor_oracle_cases={checks},validation=passed')
