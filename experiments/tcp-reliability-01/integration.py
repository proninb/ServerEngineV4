"""Real TCP correctness/lifecycle checks. Never trains PGO or measures speed."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import socket
import struct
import sys

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "experiments/production-pgo-01"))
from client import Server
from cold import native_payload
from validate_helpers import images

class PreloginServer(Server):
    def __init__(self, *args):
        self.skip_initial_login = True
        super().__init__(*args)

    def __exit__(self, kind, error, traceback):
        if kind is not None:
            # Preserve the original assertion; close only this harness process.
            self.socket.close()
            if self.process.poll() is None:
                self.process.terminate()
                self.process.wait(timeout=15)
            self.close_files()
            return False
        return super().__exit__(kind, error, traceback)

    def request(self, command, **arguments):
        if command == "LOGIN" and self.skip_initial_login:
            self.skip_initial_login = False
            return {}, 0
        return super().request(command, **arguments)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def session(root, name):
    target = root / name
    target.mkdir()
    for filename in ("server.license", "server.lease"):
        shutil.copy2(REPO / filename, target / filename)
    return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--reference", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    assert root.is_relative_to(REPO / "build") and not root.exists()
    root.mkdir(parents=True)
    binaries = {"reference": args.reference.resolve(), "candidate": args.candidate.resolve()}
    binary_hashes = {name: sha(path) for name, path in binaries.items()}
    fixture = root / "holdout"
    fixture.mkdir()
    (fixture / "types.h").write_text("struct Counter { int x; char name[8]; };")
    (fixture / "source.cpp").write_text('Counter a; a.x=7; a.name="tcp";')
    (fixture / "project.json").write_text(json.dumps({"version": 1, "name": "TCPReliabilityHoldout",
        "project": [{"name": "types", "type": "header", "path": "types.h"},
                    {"name": "source", "type": "source", "path": "source.cpp"}],
        "preprocessor": {"predefines": []}}))
    projects = {"holdout": fixture / "project.json",
                "unit": REPO / "build/production-pgo-current-02/fixtures/unit/project.json"}
    results = {"binary_hashes": binary_hashes, "arms": {}}
    for arm, exe in binaries.items():
        records = {}
        with PreloginServer(exe, session(root, arm)) as server:
            # None of these may reach server execution before LOGIN, even if
            # TCP is reachable. Name-only LOGIN remains trusted-network identity.
            for command, arguments in [("GET_STATE", {}), ("RUN", {}),
                ("PUBLISH", {"path": str(projects["holdout"])}), ("SHUTDOWN", {})]:
                response, _ = server.request(command, allow_failure=True, **arguments)
                assert response["status"] is False, (arm, command, response)
                assert server.process.poll() is None
            server.request("LOGIN", name="ArbitraryTrustedLANClient")
            assert server.request("GET_STATE")[0]["payload"]["state"] == "UNLOADED"
            for name, project in projects.items():
                server.request("PUBLISH", path=str(project))
                runtime = project.parent / ".serverengine/project.json/runtime.bin"
                record = {"images": images(project), "native": native_payload(server.name, runtime)}
                if name == "holdout":
                    assert server.request("GET_VALUE", name="a.x")[0]["payload"]["bits"] == 7
                server.request("UNLOAD")
                server.request("LOAD", path=str(project))
                assert native_payload(server.name, runtime) == record["native"]
                server.request("UNLOAD")
                records[name] = record
                print(arm, name, "PUBLISH/LOAD/artifact/native PASS", flush=True)
            if arm == "candidate":
                for cycle in range(50):
                    project = projects["holdout"]
                    server.request("PUBLISH", path=str(project))
                    assert images(project) == records["holdout"]["images"]
                    assert server.request("GET_VALUE", name="a.x")[0]["payload"]["bits"] == 7
                    server.request("RUN")
                    server.request("FREEZE")
                    server.request("UNLOAD")
                    server.request("LOAD", path=str(project))
                    assert native_payload(server.name, project.parent / ".serverengine/project.json/runtime.bin") == records["holdout"]["native"]
                    server.request("UNLOAD")
                results["candidate_lifecycle_cycles"] = 50
                # A duplicate LOGIN closes an already logged-in peer. A following
                # pipelined SHUTDOWN may already be queued, so Server must recheck
                # live session state at execution rather than trust ingress alone.
                address = server.socket.getpeername()
                packet = b""
                for command, arguments in [("LOGIN", {"name": "Duplicate"}), ("SHUTDOWN", {})]:
                    server.sequence += 1
                    data = json.dumps({"request_id": server.sequence,
                        "command": command, "arguments": arguments}).encode()
                    packet += struct.pack("!I", len(data)) + data
                server.socket.sendall(packet)
                server.socket.settimeout(3)
                try:
                    while True:
                        size = struct.unpack("!I", server.read(4))[0]
                        assert 0 < size < 16 * 1024 * 1024
                        response = json.loads(server.read(size))
                        # State actions queued before duplicate LOGIN may still
                        # arrive. They do not mean LOGIN/SHUTDOWN succeeded.
                        if response.get("request_id") in (server.sequence - 1, server.sequence):
                            assert response.get("status") is not True, response
                except (EOFError, ConnectionError):
                    pass
                server.socket.close()
                server.socket = socket.create_connection(address, timeout=5)
                server.socket.settimeout(180)
                server.request("LOGIN", name="Replacement")
                assert server.request("GET_STATE")[0]["payload"]["state"] == "UNLOADED"
                assert server.process.poll() is None
                results["failed_login_pipelined_shutdown_denied"] = True
        results["arms"][arm] = records
    assert results["arms"]["reference"] == results["arms"]["candidate"]
    assert binary_hashes == {name: sha(path) for name, path in binaries.items()}
    results["status"] = "PASS"
    results["scope"] = "Bounded 50-cycle correctness check; not a long-duration soak or performance test"
    (root / "result.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    print("TCP gates, reference parity and 50 lifecycle cycles PASS", flush=True)

if __name__ == "__main__":
    main()
