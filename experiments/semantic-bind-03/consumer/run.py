import csv
import hashlib
import json
import pathlib
import shutil
import statistics as stat
import subprocess
import sys

exe, project, output = (pathlib.Path(p).resolve() for p in sys.argv[1:4])
output.mkdir(parents=True, exist_ok=False)
here = pathlib.Path(__file__).resolve().parent
files = list(here.glob("*.cpp")) + list(here.glob("*.py")) + [here / "CMakeLists.txt"]
files += list(here.parent.glob("*.hpp"))
files += [here.parent.parent / "semantic-bind-01" / p for p in ("main.cpp", "model.hpp", "token_reuse.hpp")]
files += [here.parent.parent / "canonicalizer-01" / "kernel.hpp"]
def sha(path):
    with path.open("rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()
manifest = {"executable": str(exe), "sha256": sha(exe), "project": str(project),
            "processes_per_variant": 3, "iterations_per_process_per_access_pattern": 5,
            "sources": {str(p): sha(p) for p in files}}
(output / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
shutil.copy2(exe, output / exe.name)
rows = []
variants = ["baseline", "sparse", "shared_shape", "segmented", "combined"]
for round_id in range(3):
    order = variants[round_id:] + variants[:round_id]
    if round_id % 2:
        order.reverse()
    for variant in order:
        p = subprocess.run([str(exe), str(project), variant], capture_output=True, text=True)
        (output / f"{round_id}-{variant}.stdout.txt").write_text(p.stdout, encoding="utf-8")
        (output / f"{round_id}-{variant}.stderr.txt").write_text(p.stderr, encoding="utf-8")
        assert p.returncode == 0, p.stderr
        row = dict(f.split("=", 1) for f in p.stdout.strip().split(","))
        assert row["validation"] == "passed"
        row["round"] = round_id
        rows.append(row)
        with (output / "measurements.csv").open("w", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
        print(f"completed {round_id}-{variant}", flush=True)
assert len({r["linear_checksum"] for r in rows}) == 1
assert len({r["shuffled_checksum"] for r in rows}) == 1
summary = []
for variant in variants:
    group = [r for r in rows if r["variant"] == variant]
    summary.append({"variant": variant, "sequential_ms": stat.median(float(r["sequential_ms"]) for r in group),
                    "shuffled_ms": stat.median(float(r["shuffled_ms"]) for r in group),
                    "result_only_capacity_bytes": int(group[0]["result_only_capacity_bytes"])})
(output / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
print(json.dumps(summary, indent=2))
