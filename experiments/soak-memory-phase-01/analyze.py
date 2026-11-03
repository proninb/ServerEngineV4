"""Analyze completed external phase snapshots and virtual-memory classification."""
import argparse
from collections import Counter, defaultdict
import csv
import hashlib
import json
from pathlib import Path

MIB = 1024 * 1024
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("root", type=Path)
args = parser.parse_args()
root = args.root.resolve()
status = json.loads((root / "status.json").read_text())
assert status["status"] == "PASS_DIAGNOSTIC" and status["immutable_after_verified"]
rows = [json.loads(line) for line in (root / "phases.jsonl").read_text().splitlines()]
assert len(rows) == status["phase_samples"]
by_cycle = defaultdict(list)
for row in rows: by_cycle[row["cycle"]].append(row)
for group in by_cycle.values():
    assert [r["stage"] for r in group] == status["checkpoints"]
with (root / "phases.csv").open("w", newline="") as stream:
    writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
    writer.writeheader()
    writer.writerows(rows)
deltas = []
for cycle, group in by_cycle.items():
    for before, after in zip(group, group[1:]):
        deltas.append({"cycle": cycle, "fixture": after["fixture"], "from": before["stage"], "to": after["stage"],
            "private_delta_bytes": after["private_bytes"] - before["private_bytes"],
            "working_set_delta_bytes": after["working_set_bytes"] - before["working_set_bytes"],
            "handle_delta": after["handles"] - before["handles"]})
with (root / "stage-deltas.csv").open("w", newline="") as stream:
    writer = csv.DictWriter(stream, fieldnames=list(deltas[0]))
    writer.writeheader()
    writer.writerows(deltas)
map_data = {}
for item in status["maps"]:
    label = item["label"]
    allocations = defaultdict(Counter)
    for row in csv.DictReader((root / (label + "-regions.csv")).open()):
        if row["type"] == "MEM_PRIVATE": allocations[row["allocation_base"]][row["state"]] += int(row["size_bytes"])
    map_data[label] = allocations
high, low, rehigh = [map_data[x] for x in ("first-high", "following-low", "second-high")]
extents = lambda data: {base: sum(states.values()) for base, states in data.items()}
allocation_rows = []
for base in high.keys() | low.keys() | rehigh.keys():
    allocation_rows.append({"allocation_base": base,
        "first_high_total": sum(high[base].values()), "low_total": sum(low[base].values()), "second_high_total": sum(rehigh[base].values()),
        "first_high_committed": high[base]["MEM_COMMIT"], "low_committed": low[base]["MEM_COMMIT"], "second_high_committed": rehigh[base]["MEM_COMMIT"]})
allocation_rows.sort(key=lambda r: r["first_high_committed"] - r["low_committed"], reverse=True)
with (root / "private-allocation-comparison.csv").open("w", newline="") as stream:
    writer = csv.DictWriter(stream, fieldnames=list(allocation_rows[0]))
    writer.writeheader()
    writer.writerows(allocation_rows)
first = next(row for row in rows if row["private_bytes"] > 128 * MIB)
result = {"status": "PASS_DIAGNOSTIC", "cycles": len(by_cycle), "samples": len(rows),
    "elapsed_seconds": status["elapsed_seconds"], "first_observed_phase_above_128MiB": first,
    "transitions": status["transitions"],
    "largest_private_rises": sorted(deltas, key=lambda x: x["private_delta_bytes"], reverse=True)[:10],
    "largest_private_drops": sorted(deltas, key=lambda x: x["private_delta_bytes"])[:10],
    "per_stage_private_delta_range_bytes": {stage: [min(r["private_delta_bytes"] for r in deltas if r["to"] == stage),
        max(r["private_delta_bytes"] for r in deltas if r["to"] == stage)] for stage in status["checkpoints"][1:]},
    "handles_by_stage": {stage: [min(r["handles"] for r in rows if r["stage"] == stage), max(r["handles"] for r in rows if r["stage"] == stage)] for stage in status["checkpoints"]},
    "private_allocation_extents_equal_high_low_rehigh": extents(high) == extents(low) == extents(rehigh),
    "private_allocation_count_high": len(high),
    "private_allocations_with_changed_commit_high_low": sum(high[b]["MEM_COMMIT"] != low[b]["MEM_COMMIT"] for b in high),
    "maps": status["maps"], "immutable_after_verified": True, "release_blocked": True,
    "limits": ["Diagnostic snapshots and mapping scans perturb timing; not a repeated release gate",
        "Snapshots bracket whole commands, not lexical/parser/Graph/persistence subphases",
        "Region maps identify memory state/type/allocation base, not heap owner or live C++ objects",
        "Matching allocation extents at snapshots do not trace every allocation/decommit between them"]}
(root / "analysis.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
print(json.dumps({k: result[k] for k in ["status", "cycles", "samples", "first_observed_phase_above_128MiB", "private_allocation_extents_equal_high_low_rehigh", "private_allocations_with_changed_commit_high_low"]}, indent=2))
