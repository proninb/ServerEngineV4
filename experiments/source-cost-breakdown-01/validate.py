import hashlib
import json
import os
import pathlib
import subprocess
import sys

baseline, candidate, output=(pathlib.Path(p).resolve() for p in sys.argv[1:])
output.mkdir(parents=True,exist_ok=True)
header='struct I { int x; }; struct T { int value; int& ref; char text[4]; I inner[2]; };\n'
cases=[('scalar','T a; a.value=1;a.value=2;a.value=3;'),
       ('references','T a;T b;a.ref=b.value;a.ref=a.value;'),
       ('string','T a;a.text="abc";a.text="x";'),
       ('many','T a;T b;'+''.join(f'a.value={i};a.ref=b.value;' for i in range(260))),
       ('nested','T a;T b;a.inner[1].x=3;a.inner[1].x=4;a.ref=b.inner[0].x;'),
       ('extern','extern T a;T a;a.value=4;'),
       ('namespace','namespace N { T a;a.value=3; }'),
       ('error','T a;a.value=1;a.value=2;a.missing=3;'),
       ('forward_error','a.value=1;T a;')]
checks=0
for name,source in cases:
    folder=output/name;folder.mkdir(exist_ok=True)
    (folder/'types.h').write_text(header);(folder/'source.cpp').write_text(source)
    project={'version':1,'name':'cost','project':[{'name':'types.h','type':'header','path':'types.h'},
              {'name':'source.cpp','type':'source','path':'source.cpp'}],'preprocessor':{'predefines':[]}}
    path=folder/'project.json';path.write_text(json.dumps(project))
    expected=None
    for mode in ('baseline','off','counts','timed'):
        env=os.environ.copy();env['CW_SOURCE_COST_MODE']='off' if mode=='baseline' else mode
        exe=baseline if mode=='baseline' else candidate
        result=subprocess.run([str(exe),'publish-timeline',str(path),'2'],capture_output=True,text=True,env=env)
        (folder/f'{mode}.stdout.txt').write_text(result.stdout);(folder/f'{mode}.stderr.txt').write_text(result.stderr)
        probes=[line for line in result.stderr.splitlines() if line.startswith('SOURCE_COST,')]
        assert bool(probes)==(mode in ('counts','timed')),(name,mode,result.stderr)
        if result.returncode:
            diagnostics='\n'.join(line for line in result.stderr.splitlines() if not line.startswith('SOURCE_COST,'))
            signature=(result.returncode,diagnostics)
        else:
            artifacts=folder/'.serverengine/project.json'
            signature=(0,tuple(hashlib.sha256((artifacts/f).read_bytes()).hexdigest() for f in ('compiled.bin','runtime.bin')))
        if expected is None:expected=signature
        assert signature==expected,(name,mode,signature,expected)
        checks+=1
    assert (expected[0]!=0)==name.endswith('error'),(name,expected)
print(f'checks={checks},validation=passed')
