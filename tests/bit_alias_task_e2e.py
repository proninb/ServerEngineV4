"""Windows end-to-end regression for a Source bit alias used by SimTask."""
import argparse
import json
from pathlib import Path
import sys
import tempfile
import time

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "experiments/tcp-reliability-01"))
from integration import PreloginServer, session


def main():
    args = argparse.ArgumentParser(description=__doc__)
    args.add_argument("--exe", required=True, type=Path)
    args.add_argument("--task", required=True, type=Path)
    options = args.parse_args()
    root = Path(tempfile.mkdtemp(prefix="bit-alias-", dir=REPO / "build"))
    (root / "types.h").write_text(
        "using U_BOOL = bool; using U_INT [[cw::bits(U_BOOL, 16)]] = int16_t;")
    (root / "source.page").write_text("U_INT int1 = 5; U_BOOL& aaa; aaa = int1.1;")
    manifest = root / "project.json"
    manifest.write_text(json.dumps(dict(version=1, name="BitAlias", project=[
        dict(name="Types", type="header", path="types.h"),
        dict(name="Source", type="source", path="source.page")],
        preprocessor=dict(predefines=[]), tasks="Tasks.json")))
    (root / "Tasks.json").write_text(json.dumps(dict(version=1, tasks=[
        dict(id="BitAlias", executable=str(options.task.resolve()), enabled=True, dependencies=[],
             settings=dict(calls=1, cycles=1, starting_from_cycle=1, run_in_freeze=False))])))
    with PreloginServer(options.exe.resolve(), session(root, "server")) as server:
        server.request("LOGIN", name="BitAlias")
        server.request("PUBLISH", path=str(manifest))

        def read(name):
            return server.request("GET_VALUE", name=name)[0]["payload"]["bits"]

        assert read("aaa") == read("int1.1") == 0
        assert read("int1") == 5
        server.request("RUN")
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if read("int1") == 7:
                break
            time.sleep(.02)
        server.request("FREEZE")
        assert read("int1") == 7
        assert read("aaa") == read("int1.1") == 1
        server.request("UNLOAD")
        server.request("LOAD", path=str(manifest))
        assert read("aaa") == read("int1.1") == 0
        server.request("SET_VALUE", name="aaa", value="true")
        assert read("int1") == 7 and read("int1.1") == 1
        server.request("SET_VALUE", name="int1.1", value="false")
        assert read("int1") == 5 and read("aaa") == 0
        assert not server.request("SET_VALUE", allow_failure=True, name="int1", value="32768")[0]["status"]
        assert read("int1") == 5
        server.request("UNLOAD")
    print("SimTask bit alias PUBLISH/RUN/LOAD/SET_VALUE passed.")


if __name__ == "__main__":
    main()
