"""Post-training holdout correctness and warm real-server TCP A/B."""
import argparse
import hashlib
import json
from pathlib import Path
import random
import shutil
import statistics
import sys
from contextlib import ExitStack

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "experiments/production-pgo-01"))
from client import Server
from cold import native_payload
from validate_helpers import images


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(root):
    assert (root / "driver-status.json").is_file()
    assert json.loads((root / "driver-status.json").read_text())["completed"]
    folder = root / "holdout"
    assert not folder.exists()
    folder.mkdir()
    cases = {}
    n = 2048
    specs = {
        "strings": ("struct Item { int value; char label[64]; };",
            "\n".join(f'Item item{i}; item{i}.value={i % 997}; item{i}.label="holdout_{i}";' for i in range(n)),
            {"item0.value": 0, f"item{n-1}.value": (n-1) % 997}),
        "inheritance": ("struct Base { int base; }; struct Mid : Base { int middle; }; struct Leaf : Mid { int leaf; };",
            "\n".join(f"Leaf item{i}; item{i}.base={i+1}; item{i}.middle={i+2}; item{i}.leaf={i+3};" for i in range(n)),
            {"item0.base": 1, "item0.middle": 2, f"item{n-1}.leaf": n+2}),
        "links": ("struct I { int value; }; struct Pair { I child; I& ref; Pair():ref(child){} };",
            "\n".join(f"Pair pair{i}; I other{i}; pair{i}.ref=other{i}; other{i}.value={i+17}; pair{i}.child.value={i+3};" for i in range(n)),
            {"pair0.ref.value": 17, "pair0.child.value": 3, f"other{n-1}.value": n+16})}
    for name, (header, source, expected) in specs.items():
        target = folder / name
        target.mkdir()
        (target / "types.h").write_text(header)
        (target / "source.cpp").write_text(source)
        (target / "project.json").write_text(json.dumps({"version": 1, "name": "ReleaseHoldout_" + name,
            "project": [{"name": "types", "type": "header", "path": "types.h"},
                        {"name": "source", "type": "source", "path": "source.cpp"}],
            "preprocessor": {"predefines": []}}))
        cases[name] = {"project": str(target / "project.json"), "expected": expected,
            "inputs": {p.name: sha(p) for p in target.iterdir()}}
    (folder / "manifest.json").write_text(json.dumps(cases, indent=2), encoding="utf-8")
    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    assert root.is_relative_to(REPO / "build")
    cases = prepare(root)
    out = root / "holdout-results"
    out.mkdir()
    samples = []
    results = {}
    with ExitStack() as stack:
        servers = {}
        hashes = {}
        for arm in ("ordinary", "pgo"):
            session = out / arm
            session.mkdir()
            for filename in ("server.license", "server.lease"):
                shutil.copy2(REPO / filename, session / filename)
            exe = root / "ab-01/binaries" / arm / "ServerEngineV4.exe"
            hashes[arm] = sha(exe)
            servers[arm] = stack.enter_context(Server(exe, session))
        for name, case in cases.items():
            project = Path(case["project"])
            gold = None
            def run(arm, warmup=False):
                nonlocal gold
                server = servers[arm]
                _, elapsed = server.request("PUBLISH", path=str(project))
                runtime = project.parent / ".serverengine/project.json/runtime.bin"
                observed = {"images": images(project), "native": native_payload(server.name, runtime)}
                if gold is None: gold = observed
                assert observed == gold, (name, arm, "artifact/native parity")
                for field, expected in case["expected"].items():
                    assert server.request("GET_VALUE", name=field)[0]["payload"]["bits"] == expected
                server.request("UNLOAD")
                if warmup:
                    server.request("LOAD", path=str(project))
                    assert native_payload(server.name, runtime) == gold["native"]
                    server.request("UNLOAD")
                return elapsed
            for arm in servers: run(arm, True)
            blocks = []
            for block in range(15):
                order = ["ordinary", "pgo", "pgo", "ordinary"] if block % 2 == 0 else ["pgo", "ordinary", "ordinary", "pgo"]
                values = {"ordinary": [], "pgo": []}
                for arm in order:
                    elapsed = run(arm)
                    values[arm].append(elapsed)
                    samples.append({"case": name, "block": block, "arm": arm, "publish_request_ms": elapsed})
                    (out / "samples.json").write_text(json.dumps(samples, indent=2))
                blocks.append(statistics.mean(values["ordinary"]) - statistics.mean(values["pgo"]))
            rng = random.Random(102)
            bootstrap = sorted(statistics.mean(rng.choices(blocks, k=15)) for _ in range(20000))
            means = {arm: statistics.mean(s["publish_request_ms"] for s in samples if s["case"] == name and s["arm"] == arm) for arm in servers}
            results[name] = {"mean_ms": means, "mean_block_gain_ms": statistics.mean(blocks),
                "bootstrap_mean95": [bootstrap[500], bootstrap[19499]],
                "winning_blocks": sum(x > 0 for x in blocks), "gold": gold,
                "significant_regression_over_5_percent": bootstrap[19499] < 0 and means["pgo"] > 1.05 * means["ordinary"]}
            print(name, json.dumps({k: v for k, v in results[name].items() if k != "gold"}), flush=True)
    for name, case in cases.items():
        for filename, digest in case["inputs"].items():
            assert sha(Path(case["project"]).parent / filename) == digest
    for arm, digest in hashes.items(): assert sha(root / "ab-01/binaries" / arm / "ServerEngineV4.exe") == digest
    (out / "summary.json").write_text(json.dumps({"correctness": "PASS", "binary_hashes": hashes,
        "scope": "Three post-training synthetic holdouts, warm persistent-server protocol; not arbitrary customer projects",
        "cases": results, "calls": len(samples)}, indent=2), encoding="utf-8")

if __name__ == "__main__":
    main()
