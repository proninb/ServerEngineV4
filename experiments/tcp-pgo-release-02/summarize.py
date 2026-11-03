"""Close release-02 only after all stages finish; preserve a failed soak gate."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

REPO = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    assert root.is_relative_to(REPO / "build")
    driver = json.loads((root / "driver-status.json").read_text())
    summary = json.loads((root / "ab-01/summary.json").read_text())
    soak = json.loads((root / "soak-01/status.json").read_text())
    holdout = json.loads((root / "holdout-results/summary.json").read_text())
    validation = json.loads((root / "validation.json").read_text())
    tcp = json.loads((root / "tcp-integration/result.json").read_text())
    freeze = json.loads((root / "freeze.json").read_text())
    assert driver["completed"] and soak["status"] != "RUNNING"
    assert summary["calls"] == 60 and summary["blocks"] == 15
    assert validation["ordinary"] == validation["pgo"]
    assert tcp["status"] == "PASS" and tcp["failed_login_pipelined_shutdown_denied"]
    for arm, digest in summary["binary_hashes"].items():
        assert sha(root / "ab-01/binaries" / arm / "ServerEngineV4.exe") == digest
    old = REPO / "build/production-pgo-current-02/ab-01/binaries/pgo/ServerEngineV4.exe"
    assert sha(old) == driver["old_reference_sha256"]
    assert all(sha(root / "source" / name) == digest for name, digest in freeze["source"].items())
    for name, files in freeze["fixtures"].items():
        folder = root / "fixtures" / name
        actual = {p.relative_to(folder).as_posix() for p in folder.rglob("*") if p.is_file() and ".serverengine" not in p.relative_to(folder).parts}
        assert actual == set(files)
        assert all(sha(folder / filename) == digest for filename, digest in files.items())
    for name, case in json.loads((root / "holdout/manifest.json").read_text()).items():
        assert all(sha(root / "holdout" / name / filename) == digest for filename, digest in case["inputs"].items())
    gates = {"tcp_regression_suites": "tcp-regressions" in driver["stages"],
        "tcp_integration": tcp["status"] == "PASS", "off_use_parity": validation["ordinary"] == validation["pgo"],
        "main_ab": summary["metrics"]["publish_request_ms"]["bootstrap_mean95"][0] > 0,
        "holdout_correctness": holdout["correctness"] == "PASS" and holdout["calls"] == 180,
        "holdout_regression": not any(c["significant_regression_over_5_percent"] for c in holdout["cases"].values()),
        "preliminary_10_minute_soak": soak["status"] == "PASS_PRELIMINARY" and soak["elapsed_seconds"] >= 600}
    files = ["freeze.json", "source.patch", "ordinary-cache.txt", "use-cache.txt", "compiler/production.pgd",
        "driver-status.json", "tcp-regressions.log", "tcp-integration/result.json", "training.json", "train.log", "merge.log",
        "ab-01/samples.json", "ab-01/summary.json", "ab-01/binary-manifest.json", "validation.json",
        "holdout/manifest.json", "holdout-results/summary.json", "holdout-results/samples.json",
        "soak-01/status.json", "soak-01/samples.jsonl"]
    # Preserve the executed test tools separately from the frozen C++ inputs.
    for folder in ("tcp-pgo-release-02", "production-pgo-01", "tcp-reliability-01"):
        target = root / "harness" / folder
        target.mkdir(parents=True, exist_ok=True)
        for script in (REPO / "experiments" / folder).glob("*.py"):
            shutil.copy2(script, target / script.name)
            files.append((target / script.name).relative_to(root).as_posix())
    status = {"status": "completed_tcp_pgo_release_02_preliminary" if all(gates.values()) else "tcp_pgo_release_02_soak_or_release_gate_failed",
        "date": "2026-10-27", "base_commit": freeze["head"], "source_state": "Frozen dirty tree with TCP reliability fixes; source/ plus freeze.json is authoritative",
        "gates": gates, "release_candidate_accepted_preliminary": all(gates.values()), "deployment_approved": False,
        "production_deployed": False, "pgo_default": "OFF", "binary_hashes": summary["binary_hashes"],
        "old_reference_sha256": driver["old_reference_sha256"], "old_reference_unchanged": True,
        "timed_calls": summary["calls"], "blocks": summary["blocks"],
        "boundary": "external localhost TCP PUBLISH, startup and artifact/SHM checks excluded",
        "evidence_hashes": {f: sha(root / f) for f in files}}
    (root / "completion-status.json").write_text(json.dumps(status, indent=2), encoding="utf-8")
    target = REPO / "experiments/tcp-pgo-release-02/results/20261027"
    target.mkdir(parents=True, exist_ok=True)
    for src, dst in [("completion-status.json", "completion-status.json"),
        ("ab-01/summary.json", "unit-summary.json"), ("ab-01/samples.json", "unit-samples.json"),
        ("validation.json", "validation.json"), ("tcp-integration/result.json", "tcp-integration.json"),
        ("tcp-regressions.log", "tcp-regressions.log"), ("holdout/manifest.json", "holdout-manifest.json"),
        ("holdout-results/summary.json", "holdout-summary.json"), ("holdout-results/samples.json", "holdout-samples.json"),
        ("soak-01/status.json", "soak-status.json"), ("soak-01/samples.jsonl", "soak-samples.jsonl")]:
        shutil.copy2(root / src, target / dst)
    print(json.dumps(status, indent=2))

if __name__ == "__main__":
    main()
