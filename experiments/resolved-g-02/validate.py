"""Positive real-Parser capture/replay and a deliberate final-state corruption."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

exe, root = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
root.mkdir(parents=True, exist_ok=True)
(root / 'types.h').write_text('struct T { int value; int& ref; int arr[3]; char text[4]; };\n')
(root / 'source.cpp').write_text('''extern T a;
T a;
T b;
int extra[5];
a.value=1; a.value=2;
a.ref=b.value; a.ref=a.value;
a.arr[2]=7; a.arr[2]=8;
a.text="abc"; a.text="xyz";
extra[1]=9;
''')
project = root / 'project.json'
project.write_text(json.dumps(dict(version=1, name='ResolvedG01', project=[
    dict(name='types.h', type='header', path='types.h'),
    dict(name='source.cpp', type='source', path='source.cpp')], preprocessor=dict(predefines=[])), indent=2))

def digests():
    directory = root / '.serverengine' / project.name
    return {name: hashlib.sha256((directory / name).read_bytes()).hexdigest()
            for name in ('compiled.bin', 'runtime.bin')}

def run(capture, corrupt=False):
    env = dict(os.environ, CW_RESOLVED_G_CAPTURE=str(capture), CW_RESOLVED_G_REPEATS='3', CW_PATH_INTERN_MODE='0',
               CW_RESOLVED_G_CORRUPT=str(int(corrupt)))
    result = subprocess.run([str(exe), 'publish-timeline', str(project), '1'], env=env,
                            text=True, capture_output=True)
    stem = f'capture{capture}-corrupt{int(corrupt)}'
    (root / (stem + '.stdout')).write_text(result.stdout)
    (root / (stem + '.stderr')).write_text(result.stderr)
    return result

control = run(0)
assert control.returncode == 0, control.stderr
reference = digests()
result = run(1)
assert result.returncode == 0, result.stderr
assert digests() == reference
lines = [line for line in result.stdout.splitlines() if line.startswith('resolved_g02,')]
assert len(lines) == 90 and all('parity=1' in line for line in lines), result.stdout
summary = next(line for line in result.stdout.splitlines() if line.startswith('resolved_g01_summary,'))
counts = dict(item.split('=', 1) for item in summary.split(',')[1:])
assert int(counts['complete']) == 1 and int(counts['derive']) > 0
assert int(counts['batch']) == 2 and int(counts['link']) == 2
assert int(counts['final_objects']) == 3 and int(counts['final_links']) == 1
assert int(counts['final_inits']) == 7 and int(counts['overwritten_calls']) >= 4, counts
corrupted = run(1, True)
assert corrupted.returncode != 0 and 'parity=0' in corrupted.stdout, corrupted.stdout
print('resolved_g02: capture coverage, exact G/index parity, artifacts, duplicates, paths, extern, batches and corruption guard passed')

for mode in (8,16,24):
    env=dict(os.environ,CW_RESOLVED_G_CAPTURE='0',CW_PATH_INTERN_MODE=str(mode))
    arm=subprocess.run([str(exe),'publish-timeline',str(project),'1'],env=env,capture_output=True,text=True)
    assert arm.returncode==0, arm.stderr
    assert digests()==reference, mode
print('resolved_g02: all nine replay cases and all three production-form candidates preserve artifacts')
