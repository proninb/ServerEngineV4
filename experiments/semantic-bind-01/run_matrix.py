"""Fresh-process, sequential native measurements. Run outside the sandbox."""
import csv
import hashlib
import json
import pathlib
import statistics
import subprocess
import sys

exe, project, output = (pathlib.Path(v).resolve() for v in sys.argv[1:4])
repeats = int(sys.argv[4]) if len(sys.argv) > 4 else 5
assert 1 <= repeats <= 100
output.mkdir(parents=True, exist_ok=False)
source = pathlib.Path(__file__).resolve().parent

def sha(path):
    return hashlib.file_digest(path.open("rb"), "sha256").hexdigest()

manifest = {"exe": str(exe), "exe_sha256": sha(exe), "project": str(project),
            "project_sha256": sha(project), "repeats": repeats,
            "workers": [1, 2, 4, 8, 16], "one_warmup_per_configuration": True,
            "sources": {str(p.relative_to(source.parent.parent)): sha(p) for p in
                        [source / "main.cpp", source / "model.hpp", source / "token_reuse.hpp",
                         source.parent / "canonicalizer-01" / "kernel.hpp"]}}
(output / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
rows = []
for iteration in range(-1, repeats):
    order = [1, 2, 4, 8, 16]
    if iteration >= 0:
        turn = iteration % len(order)
        order = order[turn:] + order[:turn]
        if iteration % 2:
            order.reverse()
    for workers in order:
        tag = f"{'warmup' if iteration < 0 else iteration}-{workers}"
        p = subprocess.run([str(exe), str(project), str(workers), "1"], capture_output=True, text=True)
        (output / f"{tag}.stdout.txt").write_text(p.stdout, encoding="utf-8")
        (output / f"{tag}.stderr.txt").write_text(p.stderr, encoding="utf-8")
        if p.returncode:
            raise RuntimeError(f"{tag}: {p.stderr}")
        values = dict(item.split("=", 1) for item in p.stdout.strip().split(","))
        assert values["validation"] == "passed" and values["oracle_accepted"] == "1", values
        values.update(iteration=iteration, warmup=int(iteration < 0))
        rows.append(values)
        with (output / "measurements.csv").open("w", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
        print(f"completed {tag}", flush=True)
fields = ["roots", "effective_tokens", "token_reuse_hits", "local_symbols", "declarations",
          "binding_requests", "excluded_callables", "excluded_initializers"]
for field in fields:
    assert len({r[field] for r in rows}) == 1, field
timings = ["local_parse_ms", "combine_prepare_ms", "canonical_ms", "binding_ms", "numbering_ms", "model_total_ms"]
summary = []
for workers in [1, 2, 4, 8, 16]:
    measured = [r for r in rows if r["warmup"] == 0 and int(r["workers"]) == workers]
    row = {"workers": workers, "n": len(measured)}
    for name in timings:
        values = [float(r[name]) for r in measured]
        median = statistics.median(values)
        row[name] = median
        row[name + "_mad"] = statistics.median(abs(v - median) for v in values)
        row[name + "_min"] = min(values)
        row[name + "_max"] = max(values)
    row["process_peak_ws_bytes_median"] = statistics.median(int(r["process_peak_ws_bytes"]) for r in measured)
    summary.append(row)
with (output / "summary.csv").open("w", newline="", encoding="utf-8") as f:
    writer = csv.DictWriter(f, fieldnames=list(summary[0]))
    writer.writeheader()
    writer.writerows(summary)
(output / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
print(json.dumps(summary, indent=2))
