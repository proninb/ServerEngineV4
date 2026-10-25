"""Diagnostic counts only; never rank timings from this executable."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
repo = Path(__file__).resolve().parents[2]
exe = repo / 'build/source-link-index-01/Release/SourceLinkIndex01Audit.exe'
project = repo / 'build/publish-subsecond-01/fixture/project.json'
out = repo / 'build/source-link-index-01/audit'
out.mkdir(exist_ok=False)
golden = {'compiled.bin': 'a49506c8df1a999d68624f3a3fd0c317516015b7375deb992ced99736b75522d', 'runtime.bin': 'a0281b3bb48103a7cd339cccd072d24f3524db9932bc67b58a714906996b5ee6'}
results = {}
for reference in (True, False):
    label = 'reference' if reference else 'candidate'
    env = dict(os.environ, CW_GRAPH_LINK_INDEX_REFERENCE='1' if reference else '0')
    done = subprocess.run([str(exe), 'publish-timeline', str(project), '24027'], env=env, capture_output=True, text=True, check=True)
    (out / (label + '.stdout.txt')).write_text(done.stdout)
    (out / (label + '.stderr.txt')).write_text(done.stderr)
    row = dict(field.split('=', 1) for field in next(line for line in done.stdout.splitlines() if line.startswith('mode=')).split(','))
    results[label] = {k: int(v) for k, v in row.items() if k.startswith('link_idx_')}
    for name, expected in golden.items():
        assert hashlib.sha256((project.parent / '.serverengine' / project.name / name).read_bytes()).hexdigest() == expected
(out / 'counts.json').write_text(json.dumps(results, indent=2))
print(json.dumps(results, indent=2))
