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
variants = ["baseline", "sparse", "shared_shape", "segmented", "combined", "legacy_01"]
timings = ["local_parse_ms", "combine_prepare_ms", "canonical_ms", "binding_ms", "numbering_ms", "model_total_ms"]
summary = []
for workers in (1, 16):
    baseline = [r for r in rows if r["variant"] == "baseline" and int(r["workers"]) == workers and int(r["round"]) >= 0 and r["allocations"] == "0"]
    for variant in variants:
        group = [r for r in rows if r["variant"] == variant and int(r["workers"]) == workers and int(r["round"]) >= 0 and r["allocations"] == "0"]
        assert group and len(group) == len(baseline)
        item = {"variant": variant, "workers": workers, "n": len(group)}
        for name in timings:
            values = [float(r[name]) for r in group]
            item[name] = stat.median(values)
            item[name + "_mad"] = stat.median(abs(v - item[name]) for v in values)
        deltas = [float(next(b for b in baseline if b["round"] == r["round"])["model_total_ms"]) - float(r["model_total_ms"]) for r in group]
        item["paired_saving_median_ms"] = stat.median(deltas)
        item["paired_saving_min_ms"] = min(deltas)
        item["paired_saving_max_ms"] = max(deltas)
        item["model_capacity_bytes"] = int(group[0]["model_capacity_bytes"])
        assert all(int(r["model_capacity_bytes"]) == item["model_capacity_bytes"] for r in group)
        item["process_peak_ws_bytes_median"] = stat.median(int(r["process_peak_ws_bytes"]) for r in group)
        for r in group:
            assert int(r["binding_requests"]) == 751783 and int(r["declarations"]) == 24027
        if variant != "legacy_01":
            diagnostic = next(r for r in rows if r["variant"] == variant and int(r["workers"]) == workers and r["allocations"] == "1")
            case = root / diagnostic["case"]
            life = {r["point"]: r for r in read(case / "lifetimes.csv")}
            expected = 751783 if variant in ("segmented", "combined") else 1503566
            assert int(life["after_combine"]["live_requests"]) == expected
            assert int(life["after_worker_release"]["live_requests"]) == 751783
            memory = {r["array"]: int(r["capacity_bytes"]) for r in read(case / "memory.csv")}
            assert memory["primary_total"] == item["model_capacity_bytes"]
            if variant in ("shared_shape", "combined"):
                assert memory["copied_bound_modifiers"] == 0 and memory["bound_records"] == 751783 * 8
            if variant in ("sparse", "combined"):
                assert memory["diagnostics"] == workers * 32
            counters = {r["stage"]: r for r in read(case / "allocations.csv")}
            assert int(counters["total"]["live_bytes"]) == 0
            item["request_instance_peak"] = int(diagnostic["request_instance_peak"])
            item["tracked_peak_bytes"] = int(counters["total"]["peak_origin_live_bytes"])
            item["parser_allocations"] = int(counters["parse"]["calls"])
            item["binder_allocations"] = int(counters["binding"]["calls"])
        summary.append(item)
(root / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
fields = list(dict.fromkeys(key for r in summary for key in r))
with (root / "summary.csv").open("w", newline="", encoding="utf-8") as f:
    writer = csv.DictWriter(f, fieldnames=fields)
    writer.writeheader()
    writer.writerows(summary)
for r in summary:
    print(f"{r['workers']:2} {r['variant']:12} total={r['model_total_ms']:.3f} combine={r['combine_prepare_ms']:.3f} bind={r['binding_ms']:.3f} MiB={r['model_capacity_bytes']/1048576:.3f} peak_requests={r.get('request_instance_peak', 'n/a')}")
