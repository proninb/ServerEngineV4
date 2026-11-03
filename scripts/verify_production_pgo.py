"""Verify a completed production PGO evidence bundle; never launches or deploys."""
import argparse
import hashlib
import json
from pathlib import Path


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def contained(root, relative):
    path = (root / relative).resolve()
    if not path.is_relative_to(root):
        raise ValueError("Evidence path escapes bundle: " + relative)
    return path


def verify(bundle, source):
    status = json.loads((bundle / "completion-status.json").read_text(encoding="utf-8"))
    if status["status"] == "tcp_pgo_release_02_soak_or_release_gate_failed":
        raise ValueError("Release-02 preliminary soak/release gate failed; candidate not accepted")
    if status["status"] not in {
        "completed_pgo_benefit_confirmed_for_trained_UnitProXL_server",
        "completed_tcp_pgo_release_02_preliminary",
    }:
        raise ValueError("Experiment is incomplete or has no accepted performance result")
    for relative, expected in status["evidence_hashes"].items():
        if digest(contained(bundle, relative)) != expected:
            raise ValueError("Evidence changed: " + relative)
    freeze = json.loads((bundle / "freeze.json").read_text(encoding="utf-8"))
    expected_sources = freeze["source"]
    current_sources = {"CMakeLists.txt"}
    for folder in ("server_engine", "tests"):
        current_sources.update(p.relative_to(source).as_posix()
                               for p in (source / folder).rglob("*") if p.is_file())
    if current_sources != set(expected_sources):
        raise ValueError("Source file set changed; retrain and revalidate")
    for relative, expected in expected_sources.items():
        if digest(contained(source, relative)) != expected:
            raise ValueError("Source changed; retrain and revalidate: " + relative)
    summary = json.loads((bundle / "ab-01/summary.json").read_text(encoding="utf-8"))
    if summary["calls"] != 60 or summary["blocks"] != 15:
        raise ValueError("Unexpected experiment coverage")
    validation = json.loads((bundle / "validation.json").read_text(encoding="utf-8"))
    if validation["ordinary"] != validation["pgo"]:
        raise ValueError("Validation parity failed")
    if set(status["binary_hashes"]) != {"ordinary", "pgo"}:
        raise ValueError("Both measured arms are required")
    if status["status"] == "completed_tcp_pgo_release_02_preliminary":
        required = {"driver-status.json", "tcp-regressions.log", "tcp-integration/result.json",
            "holdout/manifest.json", "holdout-results/summary.json", "holdout-results/samples.json",
            "soak-01/status.json", "soak-01/samples.jsonl"}
        if not required.issubset(status["evidence_hashes"]):
            raise ValueError("Release-02 evidence coverage is incomplete")
        driver = json.loads((bundle / "driver-status.json").read_text(encoding="utf-8"))
        tcp = json.loads((bundle / "tcp-integration/result.json").read_text(encoding="utf-8"))
        holdout = json.loads((bundle / "holdout-results/summary.json").read_text(encoding="utf-8"))
        soak = json.loads((bundle / "soak-01/status.json").read_text(encoding="utf-8"))
        if not driver["completed"] or "tcp-regressions" not in driver["stages"]:
            raise ValueError("TCP/build validation incomplete")
        if tcp["status"] != "PASS" or not tcp["failed_login_pipelined_shutdown_denied"]:
            raise ValueError("TCP integration failed")
        if summary["metrics"]["publish_request_ms"]["bootstrap_mean95"][0] <= 0:
            raise ValueError("PGO performance gate failed")
        if holdout["correctness"] != "PASS" or holdout["calls"] != 180:
            raise ValueError("Holdout validation incomplete")
        if any(case["significant_regression_over_5_percent"] for case in holdout["cases"].values()):
            raise ValueError("Holdout regression gate failed")
        if soak["status"] != "PASS_PRELIMINARY" or soak["elapsed_seconds"] < 600:
            raise ValueError("Preliminary soak incomplete or failed")
        if soak["binary_sha256"] != status["binary_hashes"]["pgo"]:
            raise ValueError("Soak used another executable")
    for arm, expected in status["binary_hashes"].items():
        if summary["binary_hashes"][arm] != expected:
            raise ValueError("Binary manifest mismatch: " + arm)
        if digest(bundle / "ab-01/binaries" / arm / "ServerEngineV4.exe") != expected:
            raise ValueError("Measured binary changed: " + arm)
    return {"integrity": "PASS", "base_commit": status["base_commit"],
            "pgo_executable": str(bundle / "ab-01/binaries/pgo/ServerEngineV4.exe"),
            "pgo_sha256": status["binary_hashes"]["pgo"],
            "deployment_approved": False,
            "preliminary_release_02": status["status"] == "completed_tcp_pgo_release_02_preliminary",
            "scope": "Exact tested images and source snapshot; no retraining, toolchain or deployment approval"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("--source", type=Path, required=True,
                        help="Source snapshot intended for this release")
    args = parser.parse_args()
    try:
        result = verify(args.bundle.resolve(), args.source.resolve())
    except (OSError, KeyError, ValueError) as error:
        print(json.dumps({"integrity": "FAIL", "reason": str(error)}, indent=2))
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
