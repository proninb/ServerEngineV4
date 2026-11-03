"""Continue a frozen OFF build through fresh training, validation and real TCP A/B."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import datetime

REPO = Path(__file__).resolve().parents[2]
MSVC = Path(r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    assert root.is_relative_to(REPO / "build")
    status = {"stages": [], "completed": False}
    assert not (root / "driver-status.json").exists()
    source = root / "source"
    compiler = root / "compiler"
    freeze = json.loads((root / "freeze.json").read_text())
    assert all(sha(source / name) == digest for name, digest in freeze["source"].items())
    reference = REPO / "build/production-pgo-current-02/ab-01/binaries/pgo/ServerEngineV4.exe"
    status["old_reference_sha256"] = sha(reference)
    assert status["old_reference_sha256"] == "98e8c489ff1b6d03f4058151ef6833d3750289e3483656e49d76c2c5a6617bcf"

    def save():
        (root / "driver-status.json").write_text(json.dumps(status, indent=2), encoding="utf-8")

    def run(stage, arguments):
        status["stage"] = stage
        status["updated_at"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        save()
        print(stage, "START", flush=True)
        with (root / (stage + ".log")).open("w") as log:
            subprocess.run([str(a) for a in arguments], cwd=REPO, stdout=log, stderr=subprocess.STDOUT, check=True)
        status["stages"].append(stage)
        save()
        print(stage, "PASS", flush=True)

    def configure(mode):
        run("configure-" + mode.lower(), ["cmake", "-S", source, "-B", compiler,
            "-DSERVER_ENGINE_PRODUCTION_PGO=" + mode,
            "-DSERVER_ENGINE_PRODUCTION_PGO_DATABASE=" + str(compiler / "production.pgd")])

    def build(mode):
        run("build-" + mode, ["cmake", "--build", compiler, "--config", "Release",
            "--target", "ServerEngineV4", "--parallel", "16"])

    try:
        off = root / "bin/ordinary/ServerEngineV4.exe"
        off.parent.mkdir(parents=True)
        shutil.copy2(compiler / "Release/ServerEngineV4.exe", off)
        status["off_sha256"] = sha(off)
        shutil.copy2(compiler / "CMakeCache.txt", root / "ordinary-cache.txt")
        run("tcp-regressions", ["ctest", "--test-dir", compiler, "-C", "Release", "-R",
            "^(communication_|client_session$|server_policy$)", "--output-on-failure"])
        configure("GENERATE")
        build("generate")
        shutil.copy2(MSVC / "pgort140.dll", compiler / "Release/pgort140.dll")
        run("train", [sys.executable, REPO / "experiments/production-pgo-01/train.py",
            root, compiler / "Release/ServerEngineV4.exe", "3"])
        pgcs = sorted((compiler / "Release").glob("production!*.pgc"))
        assert len(pgcs) == 3
        run("merge", [MSVC / "pgomgr.exe", "/merge", *pgcs, compiler / "production.pgd"])
        status["pgd_sha256"] = sha(compiler / "production.pgd")
        configure("USE")
        build("use")
        pgo = root / "bin/pgo/ServerEngineV4.exe"
        pgo.parent.mkdir(parents=True)
        shutil.copy2(compiler / "Release/ServerEngineV4.exe", pgo)
        shutil.copy2(compiler / "CMakeCache.txt", root / "use-cache.txt")
        status["pgo_sha256"] = sha(pgo)
        run("imports-use", [MSVC / "dumpbin.exe", "/imports", pgo])
        assert "pgort" not in (root / "imports-use.log").read_text().lower()
        run("validate", [sys.executable, REPO / "experiments/production-pgo-01/validate.py", root])
        run("tcp-integration", [sys.executable, REPO / "experiments/tcp-reliability-01/integration.py",
            root / "tcp-integration", "--candidate", pgo, "--reference", off])
        run("ab", [sys.executable, REPO / "experiments/production-pgo-01/run_current.py", root, "ab-01"])
        assert sha(reference) == status["old_reference_sha256"]
        status["completed"] = True
        status["remaining"] = ["separate holdout", "10-minute preliminary soak", "release decision"]
    except Exception as error:
        status["error"] = repr(error)
        raise
    finally:
        save()

if __name__ == "__main__":
    main()
