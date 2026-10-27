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
    if status["status"] != "completed_pgo_benefit_confirmed_for_trained_UnitProXL_server":
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
    for arm, expected in status["binary_hashes"].items():
        if summary["binary_hashes"][arm] != expected:
            raise ValueError("Binary manifest mismatch: " + arm)
        if digest(bundle / "ab-01/binaries" / arm / "ServerEngineV4.exe") != expected:
            raise ValueError("Measured binary changed: " + arm)
    return {"integrity": "PASS", "base_commit": status["base_commit"],
            "pgo_executable": str(bundle / "ab-01/binaries/pgo/ServerEngineV4.exe"),
            "pgo_sha256": status["binary_hashes"]["pgo"],
            "deployment_approved": False,
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
