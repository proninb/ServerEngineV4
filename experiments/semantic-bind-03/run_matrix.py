"""Isolated single-factor variants, then combination; fresh native processes."""
import csv
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys

timing, allocations, legacy, project, output = (pathlib.Path(p).resolve() for p in sys.argv[1:6])
repeats = int(sys.argv[6]) if len(sys.argv) > 6 else 5
assert 1 <= repeats <= 30
output.mkdir(parents=True, exist_ok=False)
source = pathlib.Path(__file__).resolve().parent
repo = source.parent.parent
variants = ["baseline", "sparse", "shared_shape", "segmented", "combined", "legacy_01"]
workers_set = [1, 16]
def sha(path):
    with path.open("rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()
files = list(source.glob("*.cpp")) + list(source.glob("*.hpp")) + list(source.glob("*.py")) + [source / "CMakeLists.txt"]
files += [source.parent / "semantic-bind-01" / p for p in ("main.cpp", "model.hpp", "token_reuse.hpp", "CMakeLists.txt")]
files += [source.parent / "semantic-bind-02" / "allocation_audit.hpp", source.parent / "canonicalizer-01" / "kernel.hpp"]
manifest = {"executables": {exe.name: {"path": str(exe), "sha256": sha(exe)} for exe in (timing, allocations, legacy)},
            "project": str(project), "project_sha256": sha(project), "repeats": repeats, "workers": workers_set,
            "variants": variants, "warmup_per_configuration": 1, "legacy_role": "bridge control; variant effects use same-executable baseline",
            "sources": {str(p.relative_to(repo)): sha(p) for p in files}}
(output / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
for path in files:
    target = output / "sources" / path.relative_to(repo)
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(path, target)
for exe in (timing, allocations, legacy):
    shutil.copy2(exe, output / exe.name)
rows = []
def execute(variant, workers, round_id, diagnostic=False):
    tag = f"{'alloc' if diagnostic else round_id}-{variant}-{workers}"
    case = output / tag
    if variant == "legacy_01":
        args = [str(legacy), str(project), str(workers), "1"]
    else:
        args = [str(allocations if diagnostic else timing), str(project), str(workers), variant, str(case)]
    p = subprocess.run(args, capture_output=True, text=True)
    case.mkdir(exist_ok=True)
    (case / "stdout.txt").write_text(p.stdout, encoding="utf-8")
    (case / "stderr.txt").write_text(p.stderr, encoding="utf-8")
    if p.returncode:
        raise RuntimeError(f"{tag}: {p.stdout}\n{p.stderr}")
    row = dict(field.split("=", 1) for field in p.stdout.strip().split(","))
    assert row["validation"] == "passed" and row["oracle_accepted"] == "1"
    row.update(variant=variant, allocations=int(diagnostic), round=round_id, case=tag)
    rows.append(row)
    fields = list(dict.fromkeys(key for r in rows for key in r))
    with (output / "measurements.csv").open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    print("completed " + tag, flush=True)
for round_id in range(-1, repeats):
    order = variants[max(round_id, 0) % len(variants):] + variants[:max(round_id, 0) % len(variants)]
    if round_id % 2:
        order = list(reversed(order))
    for workers in (workers_set if round_id % 2 == 0 else reversed(workers_set)):
        for variant in order:
            execute(variant, workers, round_id)
for workers in workers_set:
    for variant in variants[:-1]:
        execute(variant, workers, 0, diagnostic=True)
print("matrix_complete")
