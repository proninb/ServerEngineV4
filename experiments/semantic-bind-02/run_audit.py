"""Sequential fresh-process observer-control matrix; allocation runs separate."""
import csv
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys

timing, allocations, project, output = (pathlib.Path(p).resolve() for p in sys.argv[1:5])
repeats = int(sys.argv[5]) if len(sys.argv) > 5 else 3
assert 1 <= repeats <= 20
output.mkdir(parents=True, exist_ok=False)
source = pathlib.Path(__file__).resolve().parent
repo = source.parent.parent
def sha(path):
    with path.open("rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()

files = list(source.glob("*.cpp")) + list(source.glob("*.hpp")) + list(source.glob("*.py")) + [source / "CMakeLists.txt"]
files += [source.parent / "semantic-bind-01" / p for p in ("main.cpp", "model.hpp", "token_reuse.hpp", "CMakeLists.txt")]
files += [source.parent / "canonicalizer-01" / "kernel.hpp"]
manifest = {"timing_exe": str(timing), "timing_sha256": sha(timing), "allocation_exe": str(allocations),
            "allocation_sha256": sha(allocations), "project": str(project), "project_sha256": sha(project),
            "pairs": repeats, "workers": [1, 2, 4, 8, 16], "one_warmup_per_mode_per_worker": True,
            "sources": {str(p.relative_to(repo)): sha(p) for p in files}}
(output / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
for path in files:
    target = output / "sources" / path.relative_to(repo)
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(path, target)
shutil.copy2(timing, output / timing.name)
shutil.copy2(allocations, output / allocations.name)
rows = []

def execute(exe, mode, workers, round_id):
    tag = f"{round_id}-{mode}-{workers}"
    case = output / tag
    p = subprocess.run([str(exe), str(project), str(workers), mode, str(case)], capture_output=True, text=True)
    case.mkdir(exist_ok=True)
    (case / "stdout.txt").write_text(p.stdout, encoding="utf-8")
    (case / "stderr.txt").write_text(p.stderr, encoding="utf-8")
    if p.returncode:
        raise RuntimeError(f"{tag}: {p.stderr}")
    with (case / "summary.csv").open(newline="", encoding="utf-8") as f:
        row = next(csv.DictReader(f))
    assert row["validation"] == "passed"
    row.update(round=round_id, case=tag)
    rows.append(row)
    with (output / "measurements.csv").open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    print("completed " + tag, flush=True)

for round_id in range(-1, repeats):
    order = [1, 2, 4, 8, 16]
    turn = max(round_id, 0) % len(order)
    order = order[turn:] + order[:turn]
    if round_id % 2:
        order.reverse()
    for workers in order:
        modes = ["control", "timing"] if (round_id + workers) % 2 else ["timing", "control"]
        for mode in modes:
            execute(timing, mode, workers, round_id)
# Do not compare these durations with the native timing executable.
for workers in (1, 2, 4, 8, 16):
    execute(allocations, "alloc", workers, 0)
print("matrix_complete")
