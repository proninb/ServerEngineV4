"""Use the independent V2 fixtures, not expectations derived from new stores."""
import ast
import csv
import json
import pathlib
import subprocess
import sys
import tempfile

timing, allocations, parent = (pathlib.Path(p).resolve() for p in sys.argv[1:4])
here = pathlib.Path(__file__).resolve().parent
tree = ast.parse((here.parent / "semantic-bind-01" / "test_oracle.py").read_text(encoding="utf-8"))
cases = next(ast.literal_eval(n.value) for n in tree.body if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == "cases" for t in n.targets))
cases += [(["struct A; struct B { A x, y; };"], False), (["struct B { Missing x, y; };"], False)]
variants = ("baseline", "sparse", "shared_shape", "segmented", "combined")
parent.mkdir(parents=True, exist_ok=True)
def csv_rows(path):
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))
def project(path, sources):
    path.mkdir()
    obj = {"version": 1, "name": "storage_fixture", "project": [], "preprocessor": {"predefines": []}}
    for i, source in enumerate(sources):
        name = f"{i}.h"
        (path / name).write_text(source, encoding="utf-8")
        obj["project"].append({"name": name, "type": "header", "path": name})
    target = path / "project.json"
    target.write_text(json.dumps(obj), encoding="utf-8")
    return target
def call(exe, path, workers, variant, output=None):
    args = [str(exe), str(path), str(workers), variant] + ([str(output)] if output else [])
    p = subprocess.run(args, capture_output=True, text=True)
    assert p.returncode == 0, (args, p.stdout, p.stderr)
    values = dict(field.split("=", 1) for field in p.stdout.strip().split(","))
    assert values["validation"] == "passed"
    return values

checks = 0
with tempfile.TemporaryDirectory(prefix="storage-", dir=parent) as folder:
    root = pathlib.Path(folder)
    for index, (sources, accepted) in enumerate(cases):
        path = project(root / str(index), sources)
        for variant in variants:
            for workers in (1, 4):
                row = call(timing, path, workers, variant)
                assert row["oracle_accepted"] == str(int(accepted)), (index, variant, row)
                assert row["candidate_error_file"] == row["oracle_error_file"]
                assert row["candidate_error_offset"] == row["oracle_error_offset"]
                checks += 1
    path = project(root / "allocation", ["namespace N { struct A; }", "namespace N { struct A { int x; }; struct B { A* p; }; }"])
    for variant in variants:
        for workers in (1, 4):
            output = root / f"allocation-{variant}-{workers}"
            row = call(allocations, path, workers, variant, output)
            life = {r["point"]: r for r in csv_rows(output / "lifetimes.csv")}
            expected_live = 2 if variant in ("segmented", "combined") else 4
            assert int(life["after_combine"]["live_requests"]) == expected_live
            assert int(life["after_worker_release"]["live_requests"]) == 2
            memory = {r["array"]: int(r["capacity_bytes"]) for r in csv_rows(output / "memory.csv")}
            assert memory["bound_records"] == 2 * (8 if variant in ("shared_shape", "combined") else 32)
            if variant in ("shared_shape", "combined"):
                assert memory["copied_bound_modifiers"] == 0
            assert int(row["diagnostic_slots"]) == (min(workers, 2) if variant in ("sparse", "combined") else 2)
            counters = {r["stage"]: r for r in csv_rows(output / "allocations.csv")}
            assert int(counters["total"]["live_bytes"]) == 0
            checks += 1
print(f"independent_oracle_and_storage_checks={checks},validation=passed")
