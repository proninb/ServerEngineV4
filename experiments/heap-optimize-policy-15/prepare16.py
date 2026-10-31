"""Prepare isolated measurement harness; never rebuild or alter HEAP-15 image."""
from pathlib import Path
import json
kit=Path('experiments/heap-optimize-performance-16');kit.mkdir(exist_ok=False)
root=Path('build/heap-optimize-performance-16');root.mkdir(exist_ok=False)
s=Path('experiments/heap-optimize-policy-15/run_soak.py').read_text()
s=s.replace('exe = root / "compiler/Release/ServerEngineV4.exe"','exe = REPO / "build/heap-optimize-policy-15/compiler/Release/ServerEngineV4.exe"')
s=s.replace('before = memory(server.process)\n                    response, elapsed', 'before = memory(server.process)\n                    wall_start=time.time_ns(); monotonic_start=time.monotonic_ns()\n                    response, elapsed')
s=s.replace('"command": command, "tcp_ms": elapsed,','"command": command, "tcp_ms": elapsed, "wall_start_ns":wall_start,\n                            "monotonic_start_ns":monotonic_start, "wall_end_ns":time.time_ns(), "monotonic_end_ns":time.monotonic_ns(),')
s=s.replace('"measured": measured, **memory(server.process)', '"measured": measured, "wall_ns":time.time_ns(), "monotonic_ns":time.monotonic_ns(), **memory(server.process)')
(kit/'run_soak.py').write_text(s)
d=Path('experiments/heap-optimize-policy-15/driver.py').read_text().replace("ROOT=Path('build/heap-optimize-policy-15').resolve()","ROOT=Path('build/heap-optimize-performance-16').resolve()")
d=d.replace("ORDER=('OFF_','EACH','POL_','EACH','POL_','OFF_')","ORDER=('OFF_','POL_','POL_','OFF_')")
d=d.replace("executable=ROOT/'compiler/Release/ServerEngineV4.exe'","executable=Path('build/heap-optimize-policy-15/compiler/Release/ServerEngineV4.exe').resolve()")
d=d.replace('experiments/heap-optimize-policy-15/run_soak.py','experiments/heap-optimize-performance-16/run_soak.py')
(kit/'driver.py').write_text(d)
(root/'calibration.json').write_bytes(Path('build/heap-optimize-policy-15/calibration.json').read_bytes())
(root/'protocol.json').write_text(json.dumps(dict(order=['OFF_','POL_','POL_','OFF_'],seconds=600,warmup_cycles=10,performance_acceptance='MEASUREMENT ONLY; no new threshold per user',threshold_bytes=120635392,gate='Original 128 MiB / 16 handles by fixture; unchanged',conditional_analysis='Join PUBLISH cycle i to UNLOADs 2*i and 2*i-1, distinguish immediate versus any prior-cycle call; stratify fixture and prior fixture',continuity='Record wall and monotonic nanoseconds; retain and flag gaps >60 seconds, never silently omit',production='UNCHANGED; PGO OFF; RELEASE BLOCKED'),indent=2))
print(kit,root)
