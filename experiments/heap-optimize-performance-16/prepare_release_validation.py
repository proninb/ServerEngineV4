from pathlib import Path
import hashlib,json
kit=Path('experiments/unload-heap-release-validation');kit.mkdir(exist_ok=False)
root=Path('build/unload-heap-release-validation');root.mkdir(exist_ok=False)
exe=Path('build/heap-integration-candidate/on/Release/ServerEngineV4.exe').resolve()
digest=hashlib.sha256(exe.read_bytes()).hexdigest()
assert digest=='a4c3e005998e463b66aefd4e9c96ac6e67274f14657a46e293687af51a4282d9'
s=Path('experiments/heap-optimize-performance-16/run_soak.py').read_text()
s=s.replace('os.environ["SE_HEAP_OPT15"] = args.arm','os.environ.pop("SE_HEAP_OPT15", None)')
s=s.replace('exe = REPO / "build/heap-optimize-policy-15/compiler/Release/ServerEngineV4.exe"','exe = REPO / "build/heap-integration-candidate/on/Release/ServerEngineV4.exe"')
s=s.replace('_, unload_ms = server.request("UNLOAD")','unload_response, unload_ms = server.request("UNLOAD")\n                with (out / "unload-responses.jsonl").open("a") as stream:\n                    stream.write(json.dumps({"cycle":index,"measured":measured,"response":unload_response})+"\\n")')
(kit/'soak.py').write_text(s)
protocol=dict(executable=str(exe),sha256=digest,seconds=600,warmup_cycles=10,threshold_bytes=120635392,memory_gate='unchanged per-fixture last5-first5 median <=128 MiB, handles <=16',production_default='OFF',PGO='OFF',adoption='NOT AUTHORIZED',scope='Exact integrated ON image; successful-path TCP soak. Separate fault tests cannot replace real OS coverage.')
(root/'protocol.json').write_text(json.dumps(protocol,indent=2))
print(json.dumps(protocol,indent=2))
