import csv
import json
import pathlib
import subprocess
import sys
import tempfile

timing, allocations, parent = (pathlib.Path(p).resolve() for p in sys.argv[1:4])
parent.mkdir(parents=True, exist_ok=True)
def rows(path):
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))

with tempfile.TemporaryDirectory(prefix="audit-", dir=parent) as folder:
    root = pathlib.Path(folder)
    project = {"version": 1, "name": "audit_fixture", "project": [], "preprocessor": {"predefines": []}}
    sources = ["namespace N { struct A; }", "namespace N { struct A { int x; }; struct B { A* p; }; }"]
    for i, source in enumerate(sources):
        name = f"{i}.h"
        (root / name).write_text(source, encoding="utf-8")
        project["project"].append({"name": name, "type": "header", "path": name})
    (root / "project.json").write_text(json.dumps(project), encoding="utf-8")
    for mode in ("control", "timing", "alloc"):
        for workers in (1, 4):
            output = root / f"{mode}-{workers}"
            exe = allocations if mode == "alloc" else timing
            p = subprocess.run([str(exe), str(root / "project.json"), str(workers), mode, str(output)], capture_output=True, text=True)
            assert p.returncode == 0, p.stderr
            summary = rows(output / "summary.csv")[0]
            assert summary["validation"] == "passed"
            if mode == "control":
                continue
            tasks = rows(output / "roots.csv")
            assert len(tasks) == 2
            assert sum(int(r["requests"]) for r in tasks) == 2
            assert all(float(r["end_ms"]) >= float(r["begin_ms"]) >= 0 for r in tasks)
            worker_rows = rows(output / "workers.csv")
            assert sum(int(r["tasks"]) for r in worker_rows) == 2
            for r in worker_rows:
                assert abs(float(r["task_wall_ms"]) + float(r["non_task_wall_ms"]) - float(summary["parse_ms"])) < 0.001
            arrays = rows(output / "arrays.csv")
            requests = {r["point"]: int(r["size"]) for r in arrays if r["array"] == "batch_requests"}
            assert requests["after_parse"] == 0 and requests["after_binding"] == 2
            life = {r["point"]: r for r in rows(output / "lifetimes.csv")}
            if mode == "alloc":
                assert int(life["after_combine_before_worker_release"]["request_instances_live"]) == 4
                assert int(life["after_worker_release"]["request_instances_live"]) == 2
                counter = {r["allocation_origin"]: r for r in rows(output / "allocations.csv")}
                assert int(counter["parse"]["calls"]) > 0
                assert int(counter["total"]["live_requested_bytes"]) == 0
                assert int(summary["request_lifetime_peak"]) >= 4
print("audit_consistency_checks=6,validation=passed")
