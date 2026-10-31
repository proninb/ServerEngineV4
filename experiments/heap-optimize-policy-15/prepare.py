from pathlib import Path
import subprocess,sys
original=Path('experiments/heap-optimize-resources-14/prepare.py').read_text(encoding='utf-8')
derived=original.replace('heap-optimize-resources-14','heap-optimize-policy-15').replace('heap_optimize14','heap_optimize15')
target=Path('build/heap-optimize-policy-15/prepare-source-executed.py')
with target.open('x',encoding='utf-8') as file:file.write(derived)
subprocess.run([sys.executable,str(target)],check=True)
text=Path('experiments/heap-optimize-resources-14/run_soak.py').read_text(encoding='utf-8')
text=text.replace('choices=("OFF_", "ON__")','choices=("OFF_", "EACH", "POL_")').replace('SE_HEAP_OPT14','SE_HEAP_OPT15')
text=text.replace('    args = parser.parse_args()','    parser.add_argument("--run-id", required=True)\n    args = parser.parse_args()',1)
text=text.replace('    out = root / ("soak-" + args.arm)','    assert args.run_id.startswith("run-") and "/" not in args.run_id and "\\\\" not in args.run_id\n    out = root / args.run_id')
text=text.replace('                server.request("PUBLISH", path=str(project))','                phase_request("PUBLISH", path=str(project))',1)
text=text.replace('                server.request("LOAD", path=str(project))','                phase_request("LOAD", path=str(project))',1)
anchor='                descriptor = server.socket.fileno()'
assert text.count(anchor)==1
helper='''                def phase_request(command, **arguments):
                    before = memory(server.process)
                    response, elapsed = server.request(command, **arguments)
                    after = memory(server.process)
                    with (out / "operation-samples.jsonl").open("a") as stream:
                        stream.write(json.dumps({"cycle": index, "case": name, "measured": measured,
                            "command": command, "tcp_ms": elapsed,
                            "page_faults_delta": (after["page_faults"] - before["page_faults"]) & 0xffffffff,
                            "before_private": before["private_bytes"], "after_private": after["private_bytes"]}) + "\\n")
                    return response
'''
text=text.replace(anchor,helper+anchor,1)
path=Path('experiments/heap-optimize-policy-15/run_soak.py')
with path.open('x',encoding='utf-8') as stream:stream.write(text)
