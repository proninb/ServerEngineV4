"""Bounded preliminary lifecycle/connection soak, with quiescent memory samples."""
import argparse
import os
from collections import Counter
import ctypes
import hashlib
import json
from pathlib import Path
import socket
import statistics
import struct
import sys
import time

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "experiments/production-pgo-01"))
sys.path.insert(0, str(REPO / "experiments/tcp-reliability-01"))
from client import Server
from integration import PreloginServer, session
from cold import native_payload
from validate_helpers import images


class Memory(ctypes.Structure):
    _fields_ = [("cb", ctypes.c_ulong), ("faults", ctypes.c_ulong)] + [
        (name, ctypes.c_size_t) for name in ("peak_ws", "ws", "peak_paged", "paged",
            "peak_nonpaged", "nonpaged", "pagefile", "peak_pagefile", "private")]


def memory(process):
    psapi = ctypes.WinDLL("psapi", use_last_error=True)
    psapi.GetProcessMemoryInfo.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ulong]
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetProcessHandleCount.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong)]
    info = Memory()
    info.cb = ctypes.sizeof(info)
    handles = ctypes.c_ulong()
    assert psapi.GetProcessMemoryInfo(int(process._handle), ctypes.byref(info), info.cb)
    assert kernel.GetProcessHandleCount(int(process._handle), ctypes.byref(handles))
    return {"private_bytes": info.private, "working_set_bytes": info.ws,
        "peak_ws_bytes": info.peak_ws, "handles": handles.value, "page_faults": info.faults}


def await_close(server):
    server.socket.settimeout(3)
    try:
        while True:
            size = struct.unpack("!I", server.read(4))[0]
            assert 0 < size < 16 * 1024 * 1024
            server.read(size)  # earlier asynchronous state actions may precede EOF
    except (EOFError, ConnectionError):
        return
    raise AssertionError("Peer did not close")


def reconnect(server, address, cycle, counts):
    server.socket.close()
    deadline = time.monotonic() + 5
    while True:
        try:
            server.socket = socket.create_connection(address, timeout=3)
            server.socket.settimeout(30)
            response, _ = Server.request(server, "SHUTDOWN", allow_failure=True)
            assert response["status"] is False
            Server.request(server, "LOGIN", name="SoakClient_" + str(cycle))
            assert Server.request(server, "GET_STATE")[0]["payload"]["state"] == "UNLOADED"
            assert server.process.poll() is None
            counts["prelogin_shutdown_denied"] += 1
            return
        except (EOFError, ConnectionError, OSError):
            server.socket.close()
            if server.process.poll() is not None or time.monotonic() >= deadline:
                raise
            counts["reconnect_retries"] += 1
            time.sleep(0.02)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--seconds", type=int, default=600)
    parser.add_argument("--arm", choices=("OFF_", "EACH", "POL_"), default="OFF_")
    parser.add_argument("--run-id", required=True)
    args = parser.parse_args()
    assert args.seconds >= 60
    root = args.root.resolve()
    assert root.is_relative_to(REPO / "build")
    bundle = REPO / "build/tcp-pgo-release-02"
    os.environ.pop("SE_HEAP_OPT15", None)
    assert args.run_id.startswith("run-") and "/" not in args.run_id and "\\" not in args.run_id
    out = root / args.run_id
    assert not out.exists()
    out.mkdir()
    manifest = json.loads((bundle / "holdout/manifest.json").read_text())
    holdouts = json.loads((bundle / "holdout-results/summary.json").read_text())
    validation = json.loads((bundle / "validation.json").read_text())
    cases = {name: {**case, "gold": holdouts["cases"][name]["gold"]} for name, case in manifest.items()}
    cases["unit"] = {"project": str(bundle / "fixtures/unit/project.json"), "expected": {},
        "gold": {key: validation["ordinary"]["unit"][key] for key in ("images", "native")}}
    exe = REPO / "build/heap-integration-candidate/on/Release/ServerEngineV4.exe"
    digest = hashlib.sha256(exe.read_bytes()).hexdigest()
    (out / "executed-harness.py").write_bytes(Path(__file__).read_bytes())
    rows = []
    counts = Counter()
    outcome = {"status": "RUNNING", "requested_seconds": args.seconds,
        "private_growth_limit_bytes": 128 * 1024 * 1024, "handle_growth_limit": 16,
        "binary_sha256": digest,
        "scope": "Preliminary 10-minute sample, synthetic holdouts plus UnitProXL; not long-duration certification"}
    (out / "status.json").write_text(json.dumps(outcome, indent=2))
    try:
        with PreloginServer(exe, session(out, "server")) as server:
            server.socket.settimeout(30)
            server.request("LOGIN", name="SoakInitial")
            address = server.socket.getpeername()
            def cycle(index, measured, started):
                name = "unit" if index % 10 == 0 else ("strings", "inheritance", "links")[index % 3]
                case = cases[name]
                project = Path(case["project"])
                runtime = project.parent / ".serverengine/project.json/runtime.bin"
                def phase_request(command, **arguments):
                    before = memory(server.process)
                    wall_start=time.time_ns(); monotonic_start=time.monotonic_ns()
                    response, elapsed = server.request(command, **arguments)
                    after = memory(server.process)
                    with (out / "operation-samples.jsonl").open("a") as stream:
                        stream.write(json.dumps({"cycle": index, "case": name, "measured": measured,
                            "command": command, "tcp_ms": elapsed, "wall_start_ns":wall_start,
                            "monotonic_start_ns":monotonic_start, "wall_end_ns":time.time_ns(), "monotonic_end_ns":time.monotonic_ns(),
                            "page_faults_delta": (after["page_faults"] - before["page_faults"]) & 0xffffffff,
                            "before_private": before["private_bytes"], "after_private": after["private_bytes"]}) + "\n")
                    return response
                descriptor = server.socket.fileno()
                peer = server.socket.getpeername()
                phase_request("PUBLISH", path=str(project))
                assert images(project) == case["gold"]["images"]
                assert native_payload(server.name, runtime) == case["gold"]["native"]
                for field, expected in case["expected"].items():
                    assert server.request("GET_VALUE", name=field)[0]["payload"]["bits"] == expected
                server.request("RUN")
                assert server.request("GET_STATE")[0]["payload"]["state"] == "RUN"
                server.request("FREEZE")
                assert server.request("GET_STATE")[0]["payload"]["state"] == "FREEZE"
                unload_response, unload_ms = server.request("UNLOAD")
                with (out / "unload-responses.jsonl").open("a") as stream:
                    stream.write(json.dumps({"cycle":index,"measured":measured,"response":unload_response})+"\n")
                assert server.socket.fileno() == descriptor and server.socket.getpeername() == peer
                with (out / "unload-timings.jsonl").open("a") as stream:
                    stream.write(json.dumps({"cycle": index, "measured": measured, "tcp_unload_ms": unload_ms}) + "\n")
                phase_request("LOAD", path=str(project))
                assert native_payload(server.name, runtime) == case["gold"]["native"]
                unload_response, unload_ms = server.request("UNLOAD")
                with (out / "unload-responses.jsonl").open("a") as stream:
                    stream.write(json.dumps({"cycle":index,"measured":measured,"response":unload_response})+"\n")
                assert server.socket.fileno() == descriptor and server.socket.getpeername() == peer
                with (out / "unload-timings.jsonl").open("a") as stream:
                    stream.write(json.dumps({"cycle": index, "measured": measured, "tcp_unload_ms": unload_ms}) + "\n")
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
                reconnect(server, address, index, counts)
                row = {"cycle": index, "case": name, "elapsed_seconds": time.monotonic() - started,
                    "measured": measured, "wall_ns":time.time_ns(), "monotonic_ns":time.monotonic_ns(), **memory(server.process)}
                rows.append(row)
                with (out / "samples.jsonl").open("a") as stream:
                    stream.write(json.dumps(row) + "\n")
                counts["cycles"] += 1
                if measured: counts["measured_cycles"] += 1
                return row
            warm_start = time.monotonic()
            for index in range(10): cycle(index, False, warm_start)
            started = time.monotonic()
            index = 10
            last_progress = started
            while time.monotonic() - started < args.seconds:
                row = cycle(index, True, started)
                index += 1
                if time.monotonic() - last_progress >= 15:
                    outcome.update({"elapsed_seconds": time.monotonic() - started,
                        "counts": dict(counts), "latest_memory": row})
                    (out / "status.json").write_text(json.dumps(outcome, indent=2))
                    print(round(outcome["elapsed_seconds"], 1), "seconds", counts["measured_cycles"], "cycles", row["private_bytes"], row["handles"], flush=True)
                    last_progress = time.monotonic()
            outcome["elapsed_seconds"] = time.monotonic() - started
            outcome["pid"] = server.process.pid
        measured = [row for row in rows if row["measured"]]
        comparison = {}
        for name in cases:
            samples = [row for row in measured if row["case"] == name]
            assert len(samples) >= 10
            comparison[name] = {field: statistics.median(row[field] for row in samples[-5:]) -
                statistics.median(row[field] for row in samples[:5]) for field in ("private_bytes", "handles")}
        outcome["quiescent_last5_minus_first5_by_case"] = comparison
        outcome["counts"] = dict(counts)
        assert all(item["private_bytes"] <= outcome["private_growth_limit_bytes"] and
            item["handles"] <= outcome["handle_growth_limit"] for item in comparison.values()), comparison
        assert hashlib.sha256(exe.read_bytes()).hexdigest() == digest
        outcome["status"] = "PASS_PRELIMINARY"
        outcome["limitations"] = ["Working set is OS-sensitive; current private bytes and handles compared at UNLOADED/reconnected checkpoints",
            "128 MiB / 16 handle gates are coarse predefined drift limits, not proof of no leaks",
            "No global allocation failure injection in production image; fault target covers synthetic OOM and terminal poll separately"]
    except Exception as error:
        outcome["status"] = "FAIL"
        outcome["error"] = repr(error)
        raise
    finally:
        (out / "status.json").write_text(json.dumps(outcome, indent=2), encoding="utf-8")

if __name__ == "__main__":
    main()
