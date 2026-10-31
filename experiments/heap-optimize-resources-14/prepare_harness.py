"""Adapt the archived gate harness without changing its fixture/order/checkpoint."""
from pathlib import Path
import hashlib,json
source=Path('build/tcp-pgo-release-02/harness/tcp-pgo-release-02/soak.py')
text=source.read_text(encoding='utf-8-sig')
text=text.replace('import argparse','import argparse\nimport os',1)
text=text.replace('    args = parser.parse_args()','    parser.add_argument("--arm", choices=("OFF_", "ON__"), default="OFF_")\n    args = parser.parse_args()',1)
needle='    out = root / "soak-01"'
assert text.count(needle)==1
text=text.replace(needle,'    bundle = REPO / "build/tcp-pgo-release-02"\n    os.environ["SE_HEAP_OPT14"] = args.arm\n    out = root / ("soak-" + args.arm)')
for name in ('holdout/manifest.json','holdout-results/summary.json','validation.json','fixtures/unit/project.json'):
 text=text.replace('root / "'+name+'"','bundle / "'+name+'"')
needle='    exe = root / "ab-01/binaries/pgo/ServerEngineV4.exe"'
assert text.count(needle)==1
text=text.replace(needle,'    exe = root / "compiler/Release/ServerEngineV4.exe"')
text=text.replace('                server.request("PUBLISH", path=str(project))','                descriptor = server.socket.fileno()\n                peer = server.socket.getpeername()\n                server.request("PUBLISH", path=str(project))',1)
needle='                server.request("UNLOAD")'
assert text.count(needle)==2
replacement='''                _, unload_ms = server.request("UNLOAD")
                assert server.socket.fileno() == descriptor and server.socket.getpeername() == peer
                with (out / "unload-timings.jsonl").open("a") as stream:
                    stream.write(json.dumps({"cycle": index, "measured": measured, "tcp_unload_ms": unload_ms}) + "\\n")'''
text=text.replace(needle,replacement)
text=text.replace('    rows = []','    (out / "executed-harness.py").write_bytes(Path(__file__).read_bytes())\n    rows = []',1)
target=Path('experiments/heap-optimize-resources-14/run_soak.py')
with target.open('x',encoding='utf-8') as stream:stream.write(text)
Path('build/heap-optimize-resources-14/harness-origin.json').write_text(json.dumps(dict(archived_soak=str(source),archived_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),modified='Paths/mode selection; same executable; external UNLOAD timing rows and same-socket assertions; unchanged warmup/order/reconnect/checkpoint/gates.'),indent=2))
