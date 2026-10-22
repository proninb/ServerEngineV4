import csv
import json
import pathlib
import statistics as stat
import sys

root = pathlib.Path(sys.argv[1]).resolve()
def read(path):
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))
rows = read(root / "measurements.csv")
summary = []
for workers in (1, 2, 4, 8, 16):
    control = [r for r in rows if int(r["workers"]) == workers and r["mode"] == "control" and int(r["round"]) >= 0]
    timed = [r for r in rows if int(r["workers"]) == workers and r["mode"] == "timing" and int(r["round"]) >= 0]
    pair_deltas = [float(t["parse_ms"]) - float(next(c for c in control if c["round"] == t["round"])["parse_ms"]) for t in timed]
    heavy, tails, utilization = [], [], []
    for r in timed:
        tasks = read(root / r["case"] / "roots.csv")
        thread_rows = read(root / r["case"] / "workers.csv")
        assert len(tasks) == int(r["roots"])
        assert sum(int(t["tokens"]) for t in tasks) == int(r["effective_tokens"])
        assert sum(int(t["requests"]) for t in tasks) == 751783
        assert sum(int(t["tasks"]) for t in thread_rows) == len(tasks)
        maximum = max(tasks, key=lambda t: float(t["wall_ms"]))
        heavy.append({"root": int(maximum["root_ordinal"]), "path": maximum["path"],
                      "tokens": int(maximum["tokens"]), "requests": int(maximum["requests"]),
                      "wall_ms": float(maximum["wall_ms"]), "share": float(maximum["wall_ms"]) / float(r["parse_ms"])})
        tails.append(stat.median(float(t["tail_wait_ms"]) for t in thread_rows))
        utilization.append(sum(float(t["task_wall_ms"]) for t in thread_rows) / (workers * float(r["parse_ms"])))
    item = {"workers": workers, "pairs": len(timed),
            "control_parse_ms": stat.median(float(r["parse_ms"]) for r in control),
            "timing_parse_ms": stat.median(float(r["parse_ms"]) for r in timed),
            "paired_observer_delta_median_ms": stat.median(pair_deltas),
            "paired_observer_delta_min_ms": min(pair_deltas), "paired_observer_delta_max_ms": max(pair_deltas),
            "longest_root": heavy[0]["path"], "longest_root_tokens": heavy[0]["tokens"], "longest_root_requests": heavy[0]["requests"],
            "longest_root_ms": stat.median(t["wall_ms"] for t in heavy),
            "longest_root_share": stat.median(t["share"] for t in heavy),
            "median_worker_tail_ms": stat.median(tails), "task_wall_utilization": stat.median(utilization)}
    assert len({t["root"] for t in heavy}) == 1
    summary.append(item)
    allocation = next(r for r in rows if r["mode"] == "alloc" and int(r["workers"]) == workers)
    case = root / allocation["case"]
    life = {r["point"]: r for r in read(case / "lifetimes.csv")}
    assert int(life["after_combine_before_worker_release"]["request_instances_live"]) == 1503566
    assert int(life["after_worker_release"]["request_instances_live"]) == 751783
    counters = {r["allocation_origin"]: r for r in read(case / "allocations.csv")}
    assert int(counters["total"]["live_requested_bytes"]) == 0
    assert int(counters["canonical"]["calls"]) > 0 and int(counters["numbering"]["calls"]) > 0
    # Framework allocations (locals vector / dispatch callable) are phase-owned
    # and deliberately have no root owner. Don't assume a std::function SBO ABI.
    assert int(counters["parse"]["calls"]) >= sum(int(r["allocation_calls"]) for r in read(case / "roots.csv"))

(root / "scaling-summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
with (root / "scaling-summary.csv").open("w", newline="", encoding="utf-8") as f:
    writer = csv.DictWriter(f, fieldnames=list(summary[0]))
    writer.writeheader()
    writer.writerows(summary)
print(json.dumps(summary, indent=2))
