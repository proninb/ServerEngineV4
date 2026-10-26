"""One historical-binary observation; caller controls sandbox execution mode."""
from pathlib import Path
import ctypes as ct
from ctypes import wintypes as wt
import json, os, subprocess, sys

repo = Path(__file__).resolve().parents[2]
out = Path(sys.argv[1]).resolve()
assert out.is_relative_to(repo/'build')
pair, label, operation = int(sys.argv[2]), sys.argv[3], sys.argv[4]
dest = out/'token-crossover'
dest.mkdir(exist_ok=True)
env = {k:v for k,v in os.environ.items()
       if not k.startswith(('CW_', 'PLS', 'FAST_ACQUIRE_', 'PUBLISH_PIPELINE_'))}
env['PLS08_CANDIDATE'] = '0'
command = [str(repo/'build/parallel-local-source-10-c9/ab-01/baseline.exe'), operation,
           str(repo/'build/parallel-local-source-10-c9/ab-01/fixture/project.json'), '0']
k32 = ct.WinDLL('kernel32',use_last_error=True)
adv = ct.WinDLL('advapi32',use_last_error=True)
k32.GetCurrentProcess.restype = wt.HANDLE
adv.OpenProcessToken.argtypes = (wt.HANDLE, wt.DWORD, ct.POINTER(wt.HANDLE))
adv.IsTokenRestricted.argtypes = (wt.HANDLE,)
k32.CloseHandle.argtypes = (wt.HANDLE,)
token = wt.HANDLE()
assert adv.OpenProcessToken(k32.GetCurrentProcess(),8,ct.byref(token))
restricted = bool(adv.IsTokenRestricted(token))
k32.CloseHandle(token)
result = subprocess.run(command,cwd=repo,env=env,capture_output=True,text=True,timeout=120)
name = f'{operation}-{pair:02}-{label}'
(dest/(name+'.stdout')).write_text(result.stdout)
(dest/(name+'.stderr')).write_text(result.stderr)
assert result.returncode == 0,(name,result.stderr)
row = {}
for line in result.stdout.splitlines():
    for item in line.split(','):
        if '=' not in item:
            continue
        key,value = item.split('=',1)
        try:
            row[key] = float(value)
        except ValueError:
            row[key] = value
assert row['pls08_candidate'] == 0
assert (row['types'],row['objects'],row['links']) == (24027,137584,367144)
row.update(pair=pair,label=label,operation=operation,is_token_restricted=restricted,command=command)
(dest/(name+'.json')).write_text(json.dumps(row,indent=2))
print(name, 'token_restricted='+str(restricted), 'total_ms='+str(row['total_ms']), flush=True)
