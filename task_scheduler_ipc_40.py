import argparse
import csv
import json
from pathlib import Path
from collections import defaultdict

def read(path):
    with path.open(newline="", encoding="utf-8-sig") as f:
        return list(csv.DictReader(f))

def stats(values):
    values = sorted(values)
    if not values:
        return {"count": 0}
    def pct(p):
        return round(values[min(len(values)-1, max(0, int((len(values)-1)*p + .999999)))], 3)
    return {"count": len(values), "p50_us": pct(.5),
            "p99_us": pct(.99), "max_us": round(values[-1], 3)}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trace-dir", required=True)
    ap.add_argument("--output-dir", required=True)
    args = ap.parse_args()

    root = Path(args.trace_dir)
    out = Path(args.output_dir)
    out.mkdir(parents=True, exist_ok=True)
    rows = []

    for parent in sorted(root.glob("parent_*.csv")):
        suffix = parent.name[len("parent_"):]
        child = root / ("child_" + suffix)
        calls = root / ("calls_" + suffix)
        if not child.exists():
            continue

        children = {(r["seq"], r["cycle"]): r for r in read(child)}
        callmap = defaultdict(list)
        if calls.exists():
            for r in read(calls):
                callmap[(r["seq"], r["cycle"])].append(r)

        for p in read(parent):
            key = (p["seq"], p["cycle"])
            c = children.get(key)
            if c is None:
                continue
            try:
                freq = int(p["frequency"])
                if freq <= 0 or freq != int(c["frequency"]):
                    continue
                a, b = int(p["qpc0"]), int(p["qpc1"])
                r, s = int(c["qpc0"]), int(c["qpc1"])
                e, d = int(c["qpc2"]), int(c["qpc3"])
                if not a <= r <= s <= e <= d <= b:
                    continue
                us = lambda x: round(x * 1e6 / freq, 3)
                calls_sum = sum(
                    int(x["end_qpc"]) - int(x["start_qpc"])
                    for x in callmap[key]
                )
                rows.append({
                    "task_mapping": suffix[:-4],
                    "seq": key[0], "cycle": key[1],
                    "roundtrip_us": us(b-a),
                    "command_to_receive_us": us(r-a),
                    "host_pre_main_us": us(s-r),
                    "main_loop_us": us(e-s),
                    "host_post_main_us": us(d-e),
                    "signal_to_parent_us": us(b-d),
                    "calls_recorded": len(callmap[key]),
                    "calls_sum_us": us(calls_sum),
                    "ok_parent": p["ok"],
                    "ok_child": c["ok"]
                })
            except (KeyError, ValueError):
                continue

    fields = [
        "task_mapping", "seq", "cycle", "roundtrip_us",
        "command_to_receive_us", "host_pre_main_us",
        "main_loop_us", "host_post_main_us",
        "signal_to_parent_us", "calls_recorded",
        "calls_sum_us", "ok_parent", "ok_child"
    ]

    for name, data in [
        ("ipc_40_cycles.csv", rows),
        ("ipc_40_top100.csv",
         sorted(rows, key=lambda x: x["roundtrip_us"], reverse=True)[:100])
    ]:
        with (out / name).open("w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=fields)
            w.writeheader()
            w.writerows(data)

    metrics = [
        "roundtrip_us", "command_to_receive_us",
        "host_pre_main_us", "main_loop_us",
        "host_post_main_us", "signal_to_parent_us",
        "calls_sum_us"
    ]
    summary = {
        "matched_rows": len(rows),
        "metrics": {k: stats([r[k] for r in rows]) for k in metrics}
    }
    (out / "ipc_40_summary.json").write_text(
        json.dumps(summary, indent=2), encoding="utf-8"
    )
    print(json.dumps(summary, indent=2))
    if not rows:
        raise SystemExit("No matched QPC rows; check trace directory")

if __name__ == "__main__":
    main()
