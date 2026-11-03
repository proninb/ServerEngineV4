"""Compare unchanged OFF/USE quiescent gate series; no phase-cost inference."""
import argparse
from collections import Counter
import csv
import hashlib
import json
from pathlib import Path
import statistics

REPO = Path(__file__).resolve().parents[2]
MIB = 1024 * 1024
THRESHOLD = 128 * MIB
MODES = {0: "duplicate_LOGIN", 1: "partial_frame_RST", 2: "oversized_frame", 3: "disconnect"}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def describe(rows, status):
    cases = {}
    for name in ("unit", "strings", "inheritance", "links"):
        group = [r for r in rows if r["case"] == name]
        first = statistics.median(r["private_bytes"] for r in group[:5])
        last = statistics.median(r["private_bytes"] for r in group[-5:])
        crossing = next((r for r in group if r["private_bytes"] > first + THRESHOLD), None)
        cases[name] = {"samples": len(group), "first5_median_private_bytes": first,
            "last5_median_private_bytes": last, "drift_bytes": last - first,
            "handles_drift": statistics.median(r["handles"] for r in group[-5:]) - statistics.median(r["handles"] for r in group[:5]),
            "first_sample_over_baseline_plus_128MiB": crossing,
            "quiescent_drift_gate_pass": last - first <= THRESHOLD}
    high = [r for r in rows if r["private_bytes"] > THRESHOLD]
    runs = []
    active = []
    for r in rows:
        if r["private_bytes"] > THRESHOLD:
            active.append(r)
        elif active:
            runs.append(active)
            active = []
    if active: runs.append(active)
    descriptions = [{"first_cycle": x[0]["cycle"], "last_cycle": x[-1]["cycle"], "samples": len(x),
        "first_seconds": x[0]["elapsed_seconds"], "last_seconds": x[-1]["elapsed_seconds"],
        "observed_checkpoint_span_seconds": x[-1]["elapsed_seconds"] - x[0]["elapsed_seconds"]} for x in runs]
    # This carries a sampled quiescent value until the next checkpoint. It does
    # NOT measure memory while PUBLISH/RUN/LOAD executes between checkpoints.
    held = sum(max(0, rows[i+1]["elapsed_seconds"] - r["elapsed_seconds"]) for i, r in enumerate(rows[:-1]) if r["private_bytes"] > THRESHOLD)
    returns = []
    for previous, current in zip(rows, rows[1:]):
        if previous["private_bytes"] > THRESHOLD and current["private_bytes"] <= THRESHOLD:
            returns.append({"cycle": current["cycle"], "case": current["case"], "seconds": current["elapsed_seconds"],
                "reconnect_mode": MODES[current["cycle"] % 4],
                "previous_private_bytes": previous["private_bytes"], "private_bytes": current["private_bytes"]})
    return {"status": status["status"], "elapsed_seconds": status["elapsed_seconds"],
        "measured_cycles": len(rows), "counts_including_warmup": status["counts"], "cases": cases,
        "private_min_bytes": min(r["private_bytes"] for r in rows), "private_max_bytes": max(r["private_bytes"] for r in rows),
        "handles_min": min(r["handles"] for r in rows), "handles_max": max(r["handles"] for r in rows),
        "first_sample_absolute_over_128MiB": high[0] if high else None,
        "high_quiescent_samples": len(high), "high_quiescent_sample_fraction": len(high) / len(rows),
        "sample_held_high_seconds_estimate": held, "high_checkpoint_runs": descriptions,
        "returns_below_128MiB": returns,
        "return_cases": dict(Counter(r["case"] for r in returns)),
        "return_reconnect_modes": dict(Counter(r["reconnect_mode"] for r in returns))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    out = args.output.resolve()
    assert root.is_relative_to(REPO / "build") and out.is_relative_to(REPO / "build")
    preflight = json.loads((out / "preflight.json").read_text())
    results = {}
    series = {}
    evidence = {}
    for arm, name in (("OFF", "soak-off-01"), ("USE", "soak-01")):
        folder = root / name
        status = json.loads((folder / "status.json").read_text())
        assert status["status"] != "RUNNING" and status["elapsed_seconds"] >= 600
        assert status["private_growth_limit_bytes"] == THRESHOLD and status["handle_growth_limit"] == 16
        rows = [json.loads(line) for line in (folder / "samples.jsonl").read_text().splitlines()]
        assert sum(not r["measured"] for r in rows) == 10
        rows = [r for r in rows if r["measured"]]
        assert len(rows) == status["counts"]["measured_cycles"]
        key = "ordinary" if arm == "OFF" else "pgo"
        assert status["binary_sha256"] == preflight["binary_hashes"][key]
        assert sha(root / "ab-01/binaries" / key / "ServerEngineV4.exe") == status["binary_sha256"]
        results[arm] = describe(rows, status)
        series[arm] = {r["cycle"]: r for r in rows}
        evidence[arm] = {"samples_sha256": sha(folder / "samples.jsonl"), "status_sha256": sha(folder / "status.json")}
    assert evidence["USE"]["samples_sha256"] == preflight["use_samples_sha256"]
    assert sha(root / "compiler/production.pgd") == preflight["pgd_sha256"]
    common = sorted(series["OFF"].keys() & series["USE"].keys())
    aligned = []
    for cycle in common:
        a, b = series["OFF"][cycle], series["USE"][cycle]
        assert a["case"] == b["case"]
        aligned.append({"cycle": cycle, "case": a["case"], "checkpoint": "UNLOADED_after_reconnect_GET_STATE",
            "reconnect_mode": MODES[cycle % 4], "off_seconds": a["elapsed_seconds"], "use_seconds": b["elapsed_seconds"],
            "off_private_MiB": a["private_bytes"] / MIB, "use_private_MiB": b["private_bytes"] / MIB,
            "use_minus_off_MiB": (b["private_bytes"] - a["private_bytes"]) / MIB,
            "off_handles": a["handles"], "use_handles": b["handles"]})
    with (out / "aligned-cycles.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(aligned[0]))
        writer.writeheader()
        writer.writerows(aligned)
    common_cases = {}
    for name in results["OFF"]["cases"]:
        group = [r for r in aligned if r["case"] == name]
        common_cases[name] = {"common_cycles": len(group),
            "median_USE_minus_OFF_MiB": statistics.median(r["use_minus_off_MiB"] for r in group),
            "last_quarter_median_USE_minus_OFF_MiB": statistics.median(r["use_minus_off_MiB"] for r in group[-max(1,len(group)//4):]),
            "OFF_common_first5_median_MiB": statistics.median(r["off_private_MiB"] for r in group[:5]),
            "OFF_common_last5_median_MiB": statistics.median(r["off_private_MiB"] for r in group[-5:]),
            "USE_common_first5_median_MiB": statistics.median(r["use_private_MiB"] for r in group[:5]),
            "USE_common_last5_median_MiB": statistics.median(r["use_private_MiB"] for r in group[-5:])}
    off_fails = any(not c["quiescent_drift_gate_pass"] for c in results["OFF"]["cases"].values())
    conclusion = "Memory gate reproduced in OFF: not exclusive to PGO; common lifecycle/allocator attribution needed" if off_fails else "OFF passed this run while prior USE failed: difference warrants phase attribution and replication; PGO causality not established"
    result = {"experiment": "SOAK-MEMORY-ATTRIBUTION-01", "date": "2026-10-27", "arms": results,
        "common_measured_cycles": len(common), "common_prefix_by_case": common_cases,
        "conclusion": conclusion, "release_blocked": True, "evidence": evidence,
        "limits": ["One OFF control compared with an earlier USE run, not randomized replicated memory A/B",
            "Checkpoint phase is unchanged and singular; no separate after-PUBLISH/after-UNLOAD observations",
            "Return case/mode associations describe completed cycles, not the operation causing deallocation",
            "Sample-held durations and high runs do not prove continuous high memory between checkpoints",
            "Different throughput means different cycle counts over 600 seconds; common-prefix comparison retained"]}
    (out / "analysis.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps({"conclusion": conclusion, "common_cycles": len(common), "arms": {a: {"status": r["status"], "cycles": r["measured_cycles"], "high_fraction": r["high_quiescent_sample_fraction"]} for a,r in results.items()}}, indent=2))

if __name__ == "__main__":
    main()
