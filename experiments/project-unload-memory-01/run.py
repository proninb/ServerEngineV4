"""Fixed TCP Project UNLOAD snapshots, optionally UMDH under per-image UST."""
import argparse
import datetime
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "experiments/production-pgo-01"))
sys.path.insert(0, str(REPO / "experiments/tcp-reliability-01"))
sys.path.insert(0, str(REPO / "experiments/tcp-pgo-release-02"))
from client import Server
from integration import PreloginServer, session
from soak import memory
from cold import native_payload
from validate_helpers import images
spec = importlib.util.spec_from_file_location("memory_phase_tools", REPO / "experiments/soak-memory-phase-01/run.py")
phase_tools = importlib.util.module_from_spec(spec)
spec.loader.exec_module(phase_tools)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--umdh", action="store_true")
    parser.add_argument("--cdb", action="store_true", help="Adaptive noninvasive heap snapshots")
    parser.add_argument('--heap-trace', action='store_true')
    args = parser.parse_args()
    assert not (args.umdh and args.cdb)
    root, out = args.root.resolve(), args.output.resolve()
    assert root.is_relative_to(REPO / "build") and out.is_relative_to(REPO / "build")
    assert not out.exists() or (args.heap_trace and not any(out.iterdir()))
    out.mkdir(exist_ok=args.heap_trace)
    bundle = REPO / "build/tcp-pgo-release-02"
    exe = root / "bin/ServerEngineV4UnloadDiag01.exe"
    tools = root / "tools/extracted/Windows Kits/10/Debuggers/x64"
    symbol_path = str(root / "bin") + ";srv*" + str(root / "symbols") + "*https://msdl.microsoft.com/download/symbols"
    env = dict(os.environ, _NT_SYMBOL_PATH=symbol_path)
    holdout = json.loads((bundle / "holdout/manifest.json").read_text())
    golds = json.loads((bundle / "holdout-results/summary.json").read_text())
    validation = json.loads((bundle / "validation.json").read_text())
    cases = {name: {**data, "gold": golds["cases"][name]["gold"]} for name, data in holdout.items()}
    cases["unit"] = {"project": str(bundle / "fixtures/unit/project.json"), "expected": {},
        "gold": {k: validation["ordinary"]["unit"][k] for k in ("images", "native")}}
    (out / "executed-harness.py").write_bytes(Path(__file__).read_bytes())
    result = {"status": "RUNNING", "umdh_enabled": args.umdh, "binary_sha256": sha(exe),
        "pdb_sha256": sha(root / "bin/ServerEngineV4.pdb"), "snapshots": [],
        "tcp_reconnections": 0, "cycles": 0, "release_gate_changed": False}
    started = time.monotonic()
    def save():
        result["elapsed_seconds"] = time.monotonic() - started
        (out / "status.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    try:
        with PreloginServer(exe, session(out, "server")) as server:
            server.socket.settimeout(60)
            server.request("LOGIN", name="PersistentUnloadClient")
            result["pid"] = server.process.pid
            if args.cdb or args.heap_trace:
                import ctypes
                creation, exit_time, kernel_time, user_time = (ctypes.c_ulonglong() for _ in range(4))
                get_times = ctypes.WinDLL('kernel32', use_last_error=True).GetProcessTimes
                get_times.argtypes = [ctypes.c_void_p] + [ctypes.POINTER(ctypes.c_ulonglong)] * 4
                assert get_times(int(server.process._handle), *map(ctypes.byref, (creation, exit_time, kernel_time, user_time)))
                result['process_creation_filetime'] = creation.value
                result['cdb_enabled'] = args.cdb
                result['heap_trace_enabled'] = args.heap_trace
            if args.heap_trace:
                (out/'target.json').write_text(json.dumps(dict(pid=result['pid'],
                    creation_filetime=result['process_creation_filetime'],binary_sha256=result['binary_sha256'])))
                deadline=time.monotonic()+150
                while not (out/'trace-ready.json').exists():
                    assert not (out/'trace-error.json').exists(), (out/'trace-error.json').read_text() if (out/'trace-error.json').exists() else ''
                    assert time.monotonic()<deadline, 'Heap recorder readiness timeout'
                    time.sleep(.25)
            descriptor = server.socket.fileno()
            peer = server.socket.getpeername()
            def checkpoint(cycle, fixture, stage):
                assert server.socket.fileno() == descriptor and server.socket.getpeername() == peer
                assert server.request("GET_STATE")[0]["payload"]["state"] == "UNLOADED"
                row = {"cycle": cycle, "fixture": fixture, "stage": stage,
                    "timestamp_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                    "elapsed_seconds": time.monotonic() - started, **memory(server.process)}
                with (out / "checkpoints.jsonl").open("a") as stream: stream.write(json.dumps(row) + "\n")
                return row
            def capture(label, row):
                print("capture", label, "private MiB", round(row["private_bytes"] / 1048576, 3), flush=True)
                record = {"label": label, "checkpoint": row}
                record["map"] = phase_tools.address_map(server, out, label, row)
                if args.cdb:
                    before = memory(server.process)
                    log = out / (label + '-cdb.txt')
                    command = '.echo HEAP_SUMMARY_BEGIN; !heap -s; .echo HEAP_STATS_BEGIN; !heap -stat; .echo ADDRESS_BEGIN; !address; .echo CAPTURE_END; q'
                    with log.open('w') as stream:
                        completed = subprocess.run([str(tools/'cdb.exe'), '-pv', '-p', str(server.process.pid),
                            '-y', symbol_path, '-c', command], env=env, stdout=stream,
                            stderr=subprocess.STDOUT, timeout=180, check=True)
                    after = memory(server.process)
                    assert server.process.poll() is None
                    assert server.request('GET_STATE')[0]['payload']['state'] == 'UNLOADED'
                    record['cdb'] = {'memory_before': before, 'memory_after': after,
                        'pid': server.process.pid, 'creation_filetime': result['process_creation_filetime'],
                        'binary_sha256': result['binary_sha256'], 'log': str(log), 'exit_code': completed.returncode}
                if args.umdh:
                    before = memory(server.process)
                    at = time.monotonic()
                    with (out / (label + "-umdh-console.txt")).open("w") as stream:
                        subprocess.run([str(tools / "umdh.exe"), "-p:" + str(server.process.pid),
                            "-f:" + str(out / (label + "-heap.txt"))], env=env, stdout=stream,
                            stderr=subprocess.STDOUT, timeout=240, check=True)
                    record["umdh"] = {"seconds": time.monotonic() - at,
                        "memory_before": before, "memory_after": memory(server.process)}
                    assert (out / (label + "-heap.txt")).is_file()
                result["snapshots"].append(record)
                save()
            capture("baseline", checkpoint(-1, "none", "before_first_publish"))
            observed_high = False
            observed_low = False
            repeated_high = False
            for cycle in range(201 if (args.cdb or args.heap_trace) else (161 if args.umdh else 111)):
                name = "unit" if cycle % 10 == 0 else ("strings", "inheritance", "links")[cycle % 3]
                case = cases[name]
                project = Path(case["project"])
                runtime = project.parent / ".serverengine/project.json/runtime.bin"
                server.request("PUBLISH", path=str(project))
                if args.heap_trace:
                    with (out/'boundaries.jsonl').open('a') as stream:
                        stream.write(json.dumps(dict(cycle=cycle,fixture=name,stage='after_publish',
                            timestamp_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),**memory(server.process)))+'\n')
                assert images(project) == case["gold"]["images"]
                assert native_payload(server.name, runtime) == case["gold"]["native"]
                for field, value in case["expected"].items():
                    assert server.request("GET_VALUE", name=field)[0]["payload"]["bits"] == value
                server.request("RUN")
                server.request("FREEZE")
                server.request("UNLOAD")
                row = checkpoint(cycle, name, "after_unload_1")
                assert images(project) == case["gold"]["images"]
                if (args.umdh and cycle >= 60) or args.cdb or args.heap_trace:
                    if not observed_high and row["private_bytes"] > 128 * 1048576:
                        capture("observed-high", row)
                        observed_high = True
                    elif observed_high and not observed_low and row["private_bytes"] <= 128 * 1048576:
                        capture("observed-low", row)
                        observed_low = True
                    elif (args.cdb or args.heap_trace) and observed_low and not repeated_high and row['private_bytes'] > 128 * 1048576:
                        capture('repeated-high', row)
                        repeated_high = True
                if not (args.cdb or args.heap_trace) and cycle in (80,100,110): capture("cycle" + str(cycle) + "-unload1", row)
                # Same TCP socket remains active; LOAD requires no re-LOGIN.
                server.request("LOAD", path=str(project))
                assert native_payload(server.name, runtime) == case["gold"]["native"]
                server.request("UNLOAD")
                checkpoint(cycle, name, "after_unload_2")
                assert images(project) == case["gold"]["images"]
                result["cycles"] += 1
                if cycle % 10 == 0:
                    print("cycle", cycle, "UNLOAD private MiB", round(row["private_bytes"] / 1048576,3), flush=True)
                    save()
                if args.umdh and cycle >= 110 and observed_high and observed_low:
                    break
                if (args.cdb or args.heap_trace) and repeated_high:
                    break
            result["observed_high_low_captured"] = observed_high and observed_low
            if args.cdb or args.heap_trace:
                result['repeated_high_captured'] = repeated_high
                assert observed_high and observed_low and repeated_high, 'Required transitions not observed within bounded run'
            result["same_TCP_socket_verified"] = True
        assert sha(exe) == result["binary_sha256"]
        result["status"] = "CAPTURE_COMPLETE"
    except Exception as error:
        result["status"] = "FAIL"
        result["error"] = repr(error)
        raise
    finally:
        if args.heap_trace:
            (out/'trace-done.flag').write_text('done')
        save()

if __name__ == "__main__":
    main()
