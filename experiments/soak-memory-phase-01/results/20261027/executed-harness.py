"""External phase memory diagnosis on the frozen OFF server; not a gate run."""
import argparse
from collections import Counter, defaultdict
import csv
import ctypes
import datetime
import hashlib
import json
from pathlib import Path
import shutil
import socket
import struct
import sys
import time

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "experiments/tcp-pgo-release-02"))
sys.path.insert(0, str(REPO / "experiments/tcp-reliability-01"))
sys.path.insert(0, str(REPO / "experiments/production-pgo-01"))
from soak import memory, await_close, reconnect
from integration import PreloginServer, session
from cold import native_payload
from validate_helpers import images

MIB = 1024 * 1024
COMMIT, RESERVE, FREE = 0x1000, 0x2000, 0x10000
TYPES = {0x20000: "MEM_PRIVATE", 0x40000: "MEM_MAPPED", 0x1000000: "MEM_IMAGE"}
STATES = {COMMIT: "MEM_COMMIT", RESERVE: "MEM_RESERVE", FREE: "MEM_FREE"}
STAGES = ("before_publish", "after_publish", "after_run_freeze", "after_unload_1",
    "after_load", "after_unload_2", "after_tcp_error", "after_reconnect_get_state")

class Region(ctypes.Structure):
    _fields_ = [("BaseAddress", ctypes.c_void_p), ("AllocationBase", ctypes.c_void_p),
        ("AllocationProtect", ctypes.c_ulong), ("PartitionId", ctypes.c_ushort),
        ("RegionSize", ctypes.c_size_t), ("State", ctypes.c_ulong),
        ("Protect", ctypes.c_ulong), ("Type", ctypes.c_ulong)]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def address_map(server, out, label, row):
    assert ctypes.sizeof(Region) == 48 and ctypes.sizeof(ctypes.c_void_p) == 8
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.VirtualQueryEx.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(Region), ctypes.c_size_t]
    kernel.VirtualQueryEx.restype = ctypes.c_size_t
    handle = int(server.process._handle)
    before = memory(server.process)
    started = time.perf_counter_ns()
    address = 0
    regions = []
    totals = defaultdict(int)
    allocations = defaultdict(int)
    while address < 1 << 63:
        region = Region()
        size = kernel.VirtualQueryEx(handle, address, ctypes.byref(region), ctypes.sizeof(region))
        if size == 0:
            error = ctypes.get_last_error()
            if error == 87 and regions:
                break
            raise ctypes.WinError(error)
        assert size == ctypes.sizeof(region) and region.RegionSize > 0
        base = region.BaseAddress or 0
        next_address = base + region.RegionSize
        assert next_address > address
        state = STATES.get(region.State, hex(region.State))
        kind = TYPES.get(region.Type, hex(region.Type)) if region.State != FREE else "undefined"
        totals[state + "/" + kind] += region.RegionSize
        if region.State == COMMIT:
            totals["COMMIT_TOTAL"] += region.RegionSize
            if region.Type == 0x20000:
                allocations[hex(region.AllocationBase or 0)] += region.RegionSize
        regions.append({"base": hex(base), "allocation_base": hex(region.AllocationBase or 0) if region.State != FREE else "undefined",
            "size_bytes": region.RegionSize, "state": state, "type": kind,
            "protect": hex(region.Protect) if region.State == COMMIT else "undefined"})
        address = next_address
    probe = Region()
    assert kernel.VirtualQueryEx(handle, 0x0000010000000000, ctypes.byref(probe), ctypes.sizeof(probe)) == ctypes.sizeof(probe)
    elapsed = (time.perf_counter_ns() - started) / 1e6
    after = memory(server.process)
    target = out / (label + "-regions.csv")
    with target.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(regions[0]))
        writer.writeheader()
        writer.writerows(regions)
    summary = {"label": label, "checkpoint": row, "query_ms": elapsed, "regions": len(regions),
        "memory_before": before, "memory_after": after, "totals_bytes": dict(totals),
        "committed_private_by_allocation_base": dict(allocations),
        "fixed_shm_probe": {"address": "0x0000010000000000", "state": STATES.get(probe.State, hex(probe.State)),
            "type": TYPES.get(probe.Type, hex(probe.Type)) if probe.State != FREE else "undefined"},
        "limits": "Non-atomic read-only region query; no page content/heap owner/live-object attribution; COW pages can retain mapped/image type"}
    (out / (label + "-summary.json")).write_text(json.dumps(summary, indent=2), encoding="utf-8")
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--max-cycles", type=int, default=180)
    parser.add_argument("--max-seconds", type=int, default=300)
    args = parser.parse_args()
    root, out = args.root.resolve(), args.output.resolve()
    assert root.is_relative_to(REPO / "build") and out.is_relative_to(REPO / "build") and not out.exists()
    out.mkdir()
    exe = root / "ab-01/binaries/ordinary/ServerEngineV4.exe"
    expected_hash = "6cd312cbc030765718ad0e2fb847723d97efc0395455052cc8a8522843cc3429"
    assert sha(exe) == expected_hash
    paths = [root / "soak-01/status.json", root / "soak-01/samples.jsonl", root / "soak-off-01/status.json",
        root / "soak-off-01/samples.jsonl", root / "compiler/production.pgd"]
    immutable = {str(p): sha(p) for p in paths}
    manifest = json.loads((root / "holdout/manifest.json").read_text())
    holdouts = json.loads((root / "holdout-results/summary.json").read_text())
    validation = json.loads((root / "validation.json").read_text())
    cases = {name: {**case, "gold": holdouts["cases"][name]["gold"]} for name, case in manifest.items()}
    cases["unit"] = {"project": str(root / "fixtures/unit/project.json"), "expected": {},
        "gold": {key: validation["ordinary"]["unit"][key] for key in ("images", "native")}}
    result = {"status": "RUNNING", "binary_sha256": expected_hash, "immutable_before": immutable,
        "checkpoints": list(STAGES), "limits": {"min_cycle": 100, "max_cycles": args.max_cycles, "max_seconds": args.max_seconds},
        "diagnostic_transition_threshold_bytes": 128 * MIB, "release_gate_changed": False,
        "observer": "External GetProcessMemoryInfo/GetProcessHandleCount at 8 phases; VirtualQueryEx at selected quiescent states",
        "maps": [], "transitions": []}
    counts = Counter()
    rows = []
    started = time.monotonic()
    transition_state = "await_high"
    (out / "executed-harness.py").write_bytes(Path(__file__).read_bytes())
    def save():
        result["elapsed_seconds"] = time.monotonic() - started
        result["counts"] = dict(counts)
        (out / "status.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    try:
        with PreloginServer(exe, session(out, "server")) as server:
            server.socket.settimeout(30)
            server.request("LOGIN", name="SoakInitial")
            address = server.socket.getpeername()
            def snapshot(index, name, stage):
                row = {"cycle": index, "fixture": name, "stage": stage, "warmup": index < 10,
                    "timestamp_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                    "elapsed_seconds": time.monotonic() - started, **memory(server.process)}
                rows.append(row)
                with (out / "phases.jsonl").open("a") as stream:
                    stream.write(json.dumps(row) + "\n")
                return row
            for index in range(args.max_cycles):
                name = "unit" if index % 10 == 0 else ("strings", "inheritance", "links")[index % 3]
                case = cases[name]
                project = Path(case["project"])
                runtime = project.parent / ".serverengine/project.json/runtime.bin"
                snapshot(index, name, STAGES[0])
                server.request("PUBLISH", path=str(project))
                snapshot(index, name, STAGES[1])
                assert images(project) == case["gold"]["images"]
                assert native_payload(server.name, runtime) == case["gold"]["native"]
                for field, expected in case["expected"].items():
                    assert server.request("GET_VALUE", name=field)[0]["payload"]["bits"] == expected
                server.request("RUN")
                assert server.request("GET_STATE")[0]["payload"]["state"] == "RUN"
                server.request("FREEZE")
                assert server.request("GET_STATE")[0]["payload"]["state"] == "FREEZE"
                snapshot(index, name, STAGES[2])
                server.request("UNLOAD")
                snapshot(index, name, STAGES[3])
                server.request("LOAD", path=str(project))
                snapshot(index, name, STAGES[4])
                assert native_payload(server.name, runtime) == case["gold"]["native"]
                server.request("UNLOAD")
                snapshot(index, name, STAGES[5])
                mode = index % 4
                if mode == 0:
                    try:
                        server.socket.settimeout(3)
                        server.request("LOGIN", name="Duplicate")
                        raise AssertionError("Duplicate LOGIN was not terminal")
                    except (EOFError, ConnectionError):
                        pass
                    counts["duplicate_login_closed"] += 1
                elif mode == 1:
                    server.socket.sendall(b"\x00\x00\x00\xff{")
                    server.socket.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("HH", 1, 0))
                    server.socket.close()
                    counts["partial_frame_reset"] += 1
                elif mode == 2:
                    server.socket.sendall(b"\xff\xff\xff\xff")
                    await_close(server)
                    counts["oversize_frame_closed"] += 1
                else:
                    server.socket.shutdown(socket.SHUT_RDWR)
                    server.socket.close()
                    counts["disconnect"] += 1
                snapshot(index, name, STAGES[6])
                reconnect(server, address, index, counts)
                row = snapshot(index, name, STAGES[7])
                counts["cycles"] += 1
                if index == 9:
                    result["maps"].append(address_map(server, out, "initial-low", row))
                if index >= 10:
                    high = row["private_bytes"] > 128 * MIB
                    label = None
                    if transition_state == "await_high" and high:
                        label, transition_state = "first-high", "await_low"
                    elif transition_state == "await_low" and not high:
                        label, transition_state = "following-low", "await_rehigh"
                    elif transition_state == "await_rehigh" and high:
                        label, transition_state = "second-high", "complete"
                    if label:
                        result["transitions"].append({"label": label, **row})
                        result["maps"].append(address_map(server, out, label, row))
                        print(label, index, name, round(row["private_bytes"] / MIB, 3), flush=True)
                if index % 10 == 0:
                    print("cycle", index, "quiescent MiB", round(row["private_bytes"] / MIB, 3), flush=True)
                    save()
                if index >= 100 and transition_state == "complete":
                    break
                if time.monotonic() - started >= args.max_seconds:
                    break
            result["transition_sequence_complete"] = transition_state == "complete"
            result["status"] = "PASS_DIAGNOSTIC" if transition_state == "complete" and rows[-1]["cycle"] >= 100 else "INCONCLUSIVE_LIMIT"
            result["pid"] = server.process.pid
        assert sha(exe) == expected_hash
        assert all(sha(Path(p)) == digest for p, digest in immutable.items())
        result["immutable_after_verified"] = True
    except Exception as error:
        result["status"] = "FAIL"
        result["error"] = repr(error)
        raise
    finally:
        result["phase_samples"] = len(rows)
        save()

if __name__ == "__main__":
    main()
