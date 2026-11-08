#!/usr/bin/env python3
"""Windows TCP + named SHM integration tests for ServerEngineV4 TASK-TIMING-SHM-01.

Runs ServerEngineV4 using an isolated clone of the Project configuration and
hardlinked/copy-on-fallback compiled.bin + runtime.bin. Never modifies the
source Project, its Tasks.json, or compiled artifacts.
"""
from __future__ import annotations

import argparse
import collections
import shutil
import ctypes
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
from datetime import datetime, timezone
import time


def check(ok: bool, message: str) -> None:
    if not ok:
        raise AssertionError(message)


def wait_until(predicate, timeout: float, description: str):
    until = time.monotonic() + timeout
    last = None
    while time.monotonic() < until:
        try:
            value = predicate()
            if value:
                return value
        except (OSError, ValueError, subprocess.SubprocessError) as exc:
            last = exc
        time.sleep(0.04)
    raise TimeoutError(f"Timed out: {description}; last error={last}")


class Client:
    def __init__(self, sock: socket.socket):
        self.sock = sock
        self.number = 0

    def command(self, name: str, arguments=None) -> dict:
        self.number += 1
        req = {"request_id": self.number, "command": name}
        if arguments is not None:
            req["arguments"] = arguments
        body = json.dumps(req, separators=(",", ":")).encode("utf-8")
        self.sock.sendall(struct.pack(">I", len(body)) + body)
        while True:
            header = self._exact(4)
            length = struct.unpack(">I", header)[0]
            check(0 < length <= 16 * 1024 * 1024, f"Invalid TCP JSON response frame: {length}")
            reply = json.loads(self._exact(length))
            if reply.get("request_id") != self.number:
                continue  # Async notification (including SERVER_STATE).
            check(reply.get("status") is True, f"{name} failed: {reply}")
            return reply

    def _exact(self, size: int) -> bytes:
        chunks = bytearray()
        while len(chunks) < size:
            b = self.sock.recv(size - len(chunks))
            if not b:
                raise ConnectionError("Server closed TCP connection")
            chunks.extend(b)
        return bytes(chunks)

    def get_state(self) -> str:
        reply = self.command("GET_STATE")
        return reply.get("payload", {}).get("state", "")


class Win32Mappings:
    FILE_MAP_READ = 0x0004

    def __init__(self):
        if sys.platform != "win32":
            raise RuntimeError("This integration test must run on Windows")
        lib = ctypes.WinDLL("kernel32", use_last_error=True)
        self.get_ticks = lib.GetTickCount64
        self.get_ticks.restype = ctypes.c_ulonglong
        self.open_mapping = lib.OpenFileMappingW
        self.open_mapping.restype = ctypes.c_void_p
        self.open_mapping.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_wchar_p]
        self.close = lib.CloseHandle
        self.close.restype = ctypes.c_int
        self.close.argtypes = [ctypes.c_void_p]

    def exists(self, name: str) -> bool:
        handle = self.open_mapping(self.FILE_MAP_READ, False, name)
        if handle:
            self.close(handle)
            return True
        return False

    def locate(self, pid: int, start_tick: int, end_tick: int) -> str:
        # FIX1 names: Local\SEV4_System_<PID>_<GetTickCount64>_<sequence>.
        # Tick window covers only the LOAD request, so no stale table can match.
        width = end_tick - start_tick
        check(0 <= width <= 120000, f"LOAD time window is invalid/too long: {width} ms")
        for instance in range(1, 12):
            for tick in range(end_tick + 2, max(0, start_tick - 2) - 1, -1):
                candidate = f"Local\\SEV4_System_{pid}_{tick}_{instance}"
                if self.exists(candidate):
                    return candidate
        raise AssertionError(
            f"System table not found under PID {pid} in LOAD time window "
            f"[{start_tick},{end_tick}]; check TASK-TIMING-SHM-01-FIX1 mapping-name contract")


def read_probe(probe: Path, name: str) -> dict:
    output = subprocess.run([str(probe), name], check=False,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            timeout=5, encoding="utf-8", errors="replace")
    check(output.returncode == 0, f"SHM probe rc={output.returncode}: {output.stderr}")
    result = json.loads(output.stdout)
    check(result.get("name") == "system", "Wrong SHM name")
    check(result.get("version") == 1, "Unsupported SHM ABI")
    check(result.get("generation", 0) > 0, "SHM was never published")
    return result


def task_index(record: dict, name: str) -> dict:
    return next(row for row in record["tasks"] if row["id"] == name)


def parse_project_json(path: Path) -> dict:
    # ServerEngineV4 accepts JSON with // and /* */ comments; Python does not.
    raw = path.read_text(encoding="utf-8-sig")
    cleaned = []
    index = 0
    string = False
    escaped = False
    while index < len(raw):
        c = raw[index]
        n = raw[index + 1] if index + 1 < len(raw) else ""
        if string:
            cleaned.append(c)
            if escaped:
                escaped = False
            elif c == "\\":
                escaped = True
            elif c == '"':
                string = False
            index += 1
            continue
        if c == '"':
            string = True
            cleaned.append(c)
            index += 1
            continue
        if c == "/" and n == "/":
            index += 2
            while index < len(raw) and raw[index] not in "\r\n":
                index += 1
            continue
        if c == "/" and n == "*":
            index += 2
            while index + 1 < len(raw) and raw[index:index + 2] != "*/":
                if raw[index] in "\r\n":
                    cleaned.append(raw[index])
                index += 1
            check(index + 1 < len(raw), "Unclosed /* comment in project.json")
            index += 2
            continue
        cleaned.append(c)
        index += 1
    try:
        config = json.loads("".join(cleaned))
    except json.JSONDecodeError as exc:
        raise AssertionError(f"Invalid project.json: {exc}") from exc
    check(isinstance(config, dict), "project.json root must be an object")
    return config


def prepare_disposable_project(original: Path, results: Path) -> tuple[Path, Path]:
    """Provide LOAD with an isolated project.json and existing runtime artifacts.

    No project.json, Tasks.json, compiled.bin or runtime.bin from the user is
    written. Artifact hardlinks are read-only inputs to LOAD, and can be kept
    alongside diagnostics without duplicating large compiled files.
    """
    config = parse_project_json(original)
    fixture_dir = results / "fixture"
    fixture_dir.mkdir(parents=True, exist_ok=False)
    clone = fixture_dir / original.name
    tasks_file = fixture_dir / "Tasks.json"
    # Append tasks last: root Project configuration's optional order is ic, tasks.
    config.pop("tasks", None)
    if isinstance(config.get("ic"), str) and config["ic"]:
        ic_path = Path(config["ic"])
        if not ic_path.is_absolute():
            config["ic"] = str((original.parent / ic_path).resolve())
    config["tasks"] = tasks_file.name

    source_artifacts = original.parent / ".serverengine" / original.name
    destination_artifacts = fixture_dir / ".serverengine" / original.name
    required = ("compiled.bin", "runtime.bin")
    for filename in required:
        check((source_artifacts / filename).is_file(),
              f"Required LOAD artifact is absent: {source_artifacts / filename}")
    destination_artifacts.mkdir(parents=True, exist_ok=True)
    for filename in required:
        source = source_artifacts / filename
        destination = destination_artifacts / filename
        try:
            os.link(source, destination)
        except OSError:
            print(f"Hardlink unavailable; copying {filename} to disposable fixture", flush=True)
            shutil.copy2(source, destination)
    clone.write_text(json.dumps(config, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Original Project unchanged: {original}", flush=True)
    print(f"Disposable E2E Project: {clone}", flush=True)
    return clone, tasks_file


def callback_log(log: Path) -> dict[tuple[int, str], set[int]]:
    data = collections.defaultdict(set)
    if log.is_file():
        for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
            fields = line.split()
            if len(fields) == 3 and fields[0].isdigit() and fields[2].isdigit():
                data[(int(fields[0]), fields[1])].add(int(fields[2]))
    return data


def config(mode: str, fast: list[Path], slow: Path | None,
           auto_stop: bool, freeze_enabled: bool = False, warn_only: bool = False) -> dict:
    tasks = []
    for index, name in enumerate(("Input", "Controller")):
        p = slow if (auto_stop or warn_only) and index == 1 else fast[index]
        assert p is not None
        tasks.append({
            "id": name,
            "executable": str(p),
            "enabled": True,
            "dependencies": ["Input"] if index == 1 and not freeze_enabled else [],
            "settings": {
                "calls": 1,
                "cycles": 1,
                "starting_from_cycle": 1,
                "run_in_freeze": freeze_enabled and index == 1
            }
        })
    timing = {"mode": mode, "period_ms": 2, "diagnostics": {
        "enabled": True,
        "deadline": {"enabled": auto_stop or warn_only, "warn_over_ms": 0.1},
        "time_drift": {"enabled": True, "warn_ahead_ms": 1000,
                       "warn_behind_ms": 1000}
    }}
    if auto_stop:
        timing["diagnostics"]["deadline"]["stop_over_ms"] = 0.1
    return {"version": 1, "timing": timing, "tasks": tasks}


def check_table(record: dict, *, mode: int, allow_stop: bool = False) -> None:
    s = record["system"]
    check(s["mode"] == mode and s["period_us"] == 2000,
          f"Wrong mode/period: {s['mode']} {s['period_us']}")
    check(record["task_count"] == 2 and len(record["tasks"]) == 2,
          "Expected two Task records")
    check({t["id"] for t in record["tasks"]} == {"Input", "Controller"},
          "Missing Task parameters")
    check(all(t["calls"] == 1 and t["cycles"] == 1 for t in record["tasks"]),
          "Invalid Task schedule in SHM")
    now_ns = time.time_ns()
    check(abs(now_ns - s["server_time_unix_ns"]) < 15_000_000_000,
          "Server UTC timestamp out of range")
    check(s["simulation_time_ns"] == s["completed_cycles"] * 2_000_000,
          "Logical time and model cycle disagree")
    if not allow_stop:
        check(s["stop_reason"] == 0, "Unexpected automatic STOP")
    check(all(t["diagnostics"]["failed"] == 0 for t in record["tasks"]),
          "Task failure in diagnostics")


def verify_exited(pid: int) -> bool:
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    op = kernel.OpenProcess
    op.restype = ctypes.c_void_p
    op.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
    close = kernel.CloseHandle
    close.argtypes = [ctypes.c_void_p]
    wait = kernel.WaitForSingleObject
    wait.restype = ctypes.c_uint32
    wait.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    handle = op(0x00100000, False, pid)  # SYNCHRONIZE
    if not handle:
        # Process gone, or access denied: access denied is not proof of exit.
        return ctypes.get_last_error() == 87  # PID no longer valid
    try:
        return wait(handle, 0) == 0
    finally:
        close(handle)


def run_scenario(client: Client, proc: subprocess.Popen, win: Win32Mappings,
                 args, tasks_file: Path, logger: Path,
                 mode: str, auto_stop: bool, frozen: bool,
                 slow: Path | None, warn_only: bool = False) -> dict:
    tasks_file.write_text(json.dumps(config(mode, args.fixtures, slow, auto_stop, frozen, warn_only),
                                     indent=2), encoding="utf-8")
    begin = win.get_ticks()
    client.command("LOAD", {"path": str(args.project)})
    end = win.get_ticks()
    name = win.locate(proc.pid, begin, end)
    print(f"{mode} auto_stop={auto_stop}: system SHM={name}", flush=True)
    initial = read_probe(args.probe, name)
    check(initial["system"]["project_state"] == 1, "SHM did not report LOADED")
    check(client.get_state() == "LOADED", "TCP did not report LOADED")
    start_log = callback_log(logger)
    client.command("RUN")
    if auto_stop:
        wait_until(lambda: client.get_state() == "STOP", args.timeout,
                   "automatic STOP after complete barrier")
        stopped = read_probe(args.probe, name)
        check_table(stopped, mode=1, allow_stop=True)
        system = stopped["system"]
        check(system["project_state"] == 6 and system["stop_reason"] == 1 and
              system["stop_pending"] == 0 and system["deadline_misses"] >= 1,
              f"STOP diagnostics invalid: {system}")
        check(system["completed_cycles"] >= 1, "STOP before completing first cycle")
        check(task_index(stopped, "Controller")["dependencies"] == [0],
              "Task dependency indices corrupted")
        for row in stopped["tasks"]:
            d = row["diagnostics"]
            check(d["scheduled"] == d["completed"] and d["completed"] >= 1,
                  f"STOP before full LOCKSTEP for {row['id']}: {d}")
        after = callback_log(logger)
        for task_id in (0, 1):
            spawned = after[(task_id, "INIT")] - start_log[(task_id, "INIT")]
            check(spawned, f"Task {task_id} did not TASKINIT")
            check(not (after[(task_id, "DONE")] - start_log[(task_id, "DONE")]),
                  f"Automatic STOP called TASKDONE for {task_id}")
            check(all(verify_exited(pid) for pid in spawned),
                  f"Task {task_id} still running after STOP")
        # The same loaded project can RUN again: new processes, new TASKINIT.
        client.command("RUN")
        wait_until(lambda: client.get_state() == "STOP", args.timeout,
                   "second auto STOP after RUN restart")
        retried = callback_log(logger)
        for task_id in (0, 1):
            new = retried[(task_id, "INIT")] - after[(task_id, "INIT")]
            check(new, f"Task {task_id} did not restart TASKINIT after STOP")
            check(all(verify_exited(pid) for pid in new),
                  f"Restarted Task {task_id} still running")
        final = read_probe(args.probe, name)
        check(final["system"]["stop_reason"] == 1,
              "Automatic STOP reason lost after restart")
    else:
        wait_until(lambda: read_probe(args.probe, name)["system"]["completed_cycles"] >= 10,
                   args.timeout, f"{mode}: ten completed cycles")
        time.sleep(args.run_seconds)
        before = read_probe(args.probe, name)
        check_table(before, mode=0 if mode == "NORMAL" else 1)
        check(before["system"]["speed_ppm"] > 0,
              "Speed factor not updated")
        if warn_only:
            check(before["system"]["deadline_enabled"] == 1 and
                  before["system"]["deadline_misses"] >= 1,
                  "NORMAL deadline diagnostics did not detect a 4ms Task")
            check(before["system"]["stop_reason"] == 0,
                  "Warning-only deadline unexpectedly stopped project")
        for row in before["tasks"]:
            check(row["diagnostics"]["completed"] > 0, f"{row['id']} never executed")
            check(row["diagnostics"]["total_calls"] == row["diagnostics"]["completed"],
                  "Mismatch TASKMAIN calls versus completed dispatches")
        check(client.get_state() == "RUN", "Expected RUN")
        client.command("FREEZE")
        check(client.get_state() == "FREEZE", "Expected FREEZE")
        paused = read_probe(args.probe, name)
        time.sleep(args.freeze_seconds)
        later = read_probe(args.probe, name)
        check(later["system"]["completed_cycles"] == paused["system"]["completed_cycles"],
              "Simulation advanced while in FREEZE")
        if frozen:
            check(task_index(later, "Input")["diagnostics"]["completed"] ==
                  task_index(paused, "Input")["diagnostics"]["completed"],
                  "Task without run_in_freeze executed in FREEZE")
            check(task_index(later, "Controller")["diagnostics"]["completed"] >
                  task_index(paused, "Controller")["diagnostics"]["completed"],
                  "Task with run_in_freeze did not execute in FREEZE")
        else:
            for tid in ("Input", "Controller"):
                check(task_index(later, tid)["diagnostics"]["completed"] ==
                      task_index(paused, tid)["diagnostics"]["completed"],
                      f"Task {tid} ran during FREEZE")
        final = later
    # From STOP/FREEZE, UNLOAD must leave socket/server usable and close SHM.
    client.command("UNLOAD")
    check(client.get_state() == "UNLOADED", "TCP GET_STATE failed after UNLOAD")
    check(proc.poll() is None, "Server exited after UNLOAD")
    check(not win.exists(name), f"System SHM leaked across UNLOAD: {name}")
    if not auto_stop:
        callbacks = callback_log(logger)
        for task_id in (0, 1):
            check(callbacks[(task_id, "DONE")] - start_log[(task_id, "DONE")],
                  f"Task {task_id} missing TASKDONE on graceful UNLOAD")
    print(f"PASS {mode}, auto_stop={auto_stop}, cycles={final['system']['completed_cycles']}, "
          f"speed={final['system']['speed_ppm'] / 1_000_000:.2f}x", flush=True)
    return final


def run(args) -> None:
    check(sys.platform == "win32", "Windows required")
    check(args.host in ("127.0.0.1", "localhost", "::1"),
          "Only local ServerEngineV4 instances are supported")
    try:
        with socket.create_connection((args.host, args.port), timeout=0.4):
            raise AssertionError(f"TCP port {args.port} already serves another process")
    except (ConnectionRefusedError, TimeoutError, OSError):
        pass
    for file in [args.server, args.server_config, args.project, args.probe,
                 *args.fixtures, args.slow_fixture]:
        check(file is not None and file.is_file(), f"Missing required file: {file}")
    diagnostics = args.results_dir or (
        Path.cwd() / "build" / "task-timing-e2e-02" /
        datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S-%f"))
    check(not diagnostics.exists(), f"Results folder already exists: {diagnostics}")
    diagnostics.mkdir(parents=True)
    if args.tasks_file is not None:
        print("--tasks-file is ignored: Tasks.json is created inside an isolated fixture", flush=True)
    args.project, tasks_path = prepare_disposable_project(args.project, diagnostics)
    print(f"E2E results: {diagnostics}", flush=True)
    logger = diagnostics / "task_calls.log"
    server_log = diagnostics / "server.log"
    env = dict(os.environ)
    env["TASK_TIMING_E2E_LOG"] = str(logger)
    env.pop("TASK_PERF52_NO_BARRIER", None)
    env.pop("TASK_SCHEDULER_EXECUTION", None)
    env.pop("TASK_SCHEDULER_THREAD", None)
    env.pop("TASK_EMPTY_MAIN", None)
    env["TASK_SCHEDULER_TIMER"] = "HRTIMER"
    win = Win32Mappings()
    try:
        with server_log.open("wb") as output:
            proc = subprocess.Popen([str(args.server), str(args.server_config)],
                                    cwd=str(args.server.parent), env=env,
                                    stdout=output, stderr=subprocess.STDOUT)
            try:
                def connect():
                    check(proc.poll() is None, f"Server exited rc={proc.returncode}; see {server_log}")
                    try:
                        return socket.create_connection((args.host, args.port), timeout=0.4)
                    except OSError:
                        return None
                sock = wait_until(connect, args.startup_timeout, "Server TCP listener")
                with sock:
                    sock.settimeout(args.timeout)
                    client = Client(sock)
                    client.command("LOGIN", {"name": args.login})
                    for label, mode, auto_stop, frozen, slow, warn_only in (
                            ("normal", "NORMAL", False, True, None, False),
                            ("normal_deadline", "NORMAL", False, False, args.slow_fixture, True),
                            ("realtime", "REAL_TIME", False, False, None, False),
                            ("auto_stop", "REAL_TIME", True, False, args.slow_fixture, False)):
                        data = run_scenario(client, proc, win, args, tasks_path, logger,
                                            mode, auto_stop, frozen, slow, warn_only)
                        (diagnostics / f"{label}.json").write_text(
                            json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")
                    client.command("SHUTDOWN")
                proc.wait(timeout=args.shutdown_timeout)
                check(proc.returncode == 0,
                      f"Server exited with {proc.returncode}; see {server_log}")
            finally:
                if proc.poll() is None:
                    proc.terminate()
                    try:
                        proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                        proc.wait(timeout=5)
    finally:
        # Only files in the disposable fixture were modified; retain for review.
        print(f"Disposable fixture retained: {args.project}", flush=True)
    if logger.is_file():
        events = callback_log(logger)
        check(not events[(0, "NO_SHM")] and not events[(1, "NO_SHM")],
              "Task fixture failed to attach Project SHM")
    print("TASK-TIMING-E2E-02 PASS: NORMAL / REAL_TIME / FREEZE / AUTO STOP / RESTART / SHM / TCP", flush=True)

def path_arg(value: str) -> Path:
    return Path(value).expanduser().resolve()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", required=True, type=path_arg)
    parser.add_argument("--server-config", required=True, type=path_arg)
    parser.add_argument("--project", required=True, type=path_arg,
                        help="Existing project.json with compiled.bin and runtime.bin (never modified)")
    parser.add_argument("--tasks-file", type=path_arg)
    parser.add_argument("--allow-external-tasks", action="store_true", help="Legacy, ignored")
    parser.add_argument("--probe", required=True, type=path_arg)
    parser.add_argument("--fixtures", required=True, nargs=2, type=path_arg)
    parser.add_argument("--slow-fixture", required=True, type=path_arg)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", required=True, type=int)
    parser.add_argument("--login", default="TaskE2E")
    parser.add_argument("--startup-timeout", type=float, default=30)
    parser.add_argument("--shutdown-timeout", type=float, default=10)
    parser.add_argument("--timeout", type=float, default=25)
    parser.add_argument("--results-dir", type=path_arg)
    parser.add_argument("--run-seconds", type=float, default=0.2)
    parser.add_argument("--freeze-seconds", type=float, default=0.15)
    args = parser.parse_args()
    try:
        run(args)
    except Exception as exc:
        print(f"TASK-TIMING-E2E-02 FAIL: {type(exc).__name__}: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
