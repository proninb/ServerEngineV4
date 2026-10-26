"""Historical/current reference/current fast, exact original fixture, no rebuild."""
from pathlib import Path
import ctypes as ct
from ctypes import wintypes as wt
import hashlib, itertools, json, os, platform, random, shutil, statistics, subprocess, sys, time

repo = Path(__file__).resolve().parents[2]
out = Path(sys.argv[1]).resolve()
label = sys.argv[2]
blocks = int(sys.argv[3]) if len(sys.argv) > 3 else 15
profiles = int(sys.argv[4]) if len(sys.argv) > 4 else 7
assert out.is_relative_to(repo/'build')
dest = out/label
dest.mkdir(exist_ok=False)
logs = dest/'logs'
logs.mkdir()
fixture = repo/'build/parallel-local-source-10-c9/ab-01/fixture'
project = fixture/'project.json'
artifact = fixture/'.serverengine/project.json'
old = repo/'build/parallel-local-source-10-c9/ab-01/baseline.exe'
modern = repo/'build/publish-fast-acquire-02/binaries/PublishFastAcquire02.exe'
names = ('compiled.bin', 'runtime.bin')

def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

assert sha(old) == '841c7848aaf4631fef25c8bf6eb28de7cac072f9b01dd66dcf0cae7b0a1adde5'
assert sha(modern) == '1a356c04c7d0458a35c1be877b5374f120417d79a663ecf4fd347c105ec448ab'
inputs = {p.relative_to(fixture).as_posix():sha(p) for p in sorted(fixture.rglob('*'))
          if p.is_file() and '.serverengine' not in p.relative_to(fixture).parts}
(dest/'input-manifest.json').write_text(json.dumps(inputs, indent=2))
base = {k:v for k,v in os.environ.items()
        if not k.startswith(('CW_', 'PLS', 'FAST_ACQUIRE_', 'PUBLISH_PIPELINE_'))}
base['PLS08_CANDIDATE'] = '0'

k32 = ct.WinDLL('kernel32', use_last_error=True)
adv = ct.WinDLL('advapi32', use_last_error=True)
psapi = ct.WinDLL('psapi', use_last_error=True)
k32.GetCurrentProcess.restype = wt.HANDLE
adv.OpenProcessToken.argtypes = (wt.HANDLE, wt.DWORD, ct.POINTER(wt.HANDLE))
adv.IsTokenRestricted.argtypes = (wt.HANDLE,)
adv.GetTokenInformation.argtypes = (wt.HANDLE, ct.c_int, ct.c_void_p, wt.DWORD, ct.POINTER(wt.DWORD))
k32.CloseHandle.argtypes = (wt.HANDLE,)
handle = wt.HANDLE()
assert adv.OpenProcessToken(k32.GetCurrentProcess(), 8, ct.byref(handle))
token = dict(is_restricted=bool(adv.IsTokenRestricted(handle)))
for key, kind in (('has_restrictions', 21), ('is_app_container', 29)):
    value, size = wt.DWORD(), wt.DWORD()
    token[key] = int(value.value) if adv.GetTokenInformation(handle, kind, ct.byref(value),
                                      ct.sizeof(value), ct.byref(size)) else None
    if token[key] is not None:
        token[key] = int(value.value)
k32.CloseHandle(handle)

class FileTime(ct.Structure):
    _fields_ = [('low', wt.DWORD), ('high', wt.DWORD)]

def ms(value):
    return ((value.high << 32) | value.low) / 10000

class Memory(ct.Structure):
    _fields_ = [('cb', wt.DWORD), ('PageFaultCount', wt.DWORD)] + [
        (name, ct.c_size_t) for name in ('PeakWorkingSetSize', 'WorkingSetSize',
        'QuotaPeakPagedPoolUsage', 'QuotaPagedPoolUsage', 'QuotaPeakNonPagedPoolUsage',
        'QuotaNonPagedPoolUsage', 'PagefileUsage', 'PeakPagefileUsage')]

k32.GetProcessTimes.argtypes = (wt.HANDLE,) + (ct.POINTER(FileTime),)*4
psapi.GetProcessMemoryInfo.argtypes = (wt.HANDLE, ct.POINTER(Memory), wt.DWORD)
k32.GetSystemTimes.argtypes = (ct.POINTER(FileTime),)*3

def system_times():
    values = [FileTime() for _ in range(3)]
    assert k32.GetSystemTimes(*(ct.byref(v) for v in values))
    return [ms(v) for v in values]

metadata = dict(label=label, token=token, os=platform.platform(), logical_cpus=os.cpu_count(),
                project=str(project), fixture_manifest_count=len(inputs),
                binaries={name:dict(path=str(path), sha256=sha(path))
                          for name,path in (('A',old),('BC',modern))},
                cwd=str(repo), measured_timer='native total_ms, before resident teardown',
                arms=dict(A='historical V2, PLS08_CANDIDATE=0',
                          B='modern metadata-check reference', C='modern fast reader'))
(dest/'metadata.json').write_text(json.dumps(metadata, indent=2))
backup = dest/'original-artifacts'
backup.mkdir()
for name in names:
    shutil.copy2(artifact/name, backup/name)
samples, checks, gold, shm = [], [], None, None

def fields(text):
    row = {}
    for line in text.splitlines():
        for part in line.split(','):
            if '=' not in part:
                continue
            key,value = part.split('=',1)
            try:
                row[key] = float(value)
            except ValueError:
                row[key] = value
    return row

def run(arm, operation, tag, audit=False):
    global gold, shm
    env = dict(base)
    if arm != 'A':
        env['FAST_ACQUIRE_02_REFERENCE'] = '1' if arm == 'B' else '0'
    if audit:
        env['CW_RUNTIME_SHM_AUDIT'] = '1'
    command = [str(old if arm == 'A' else modern), operation, str(project),
               '0' if arm == 'A' else '24027']
    begin = system_times()
    start = time.perf_counter()
    proc = subprocess.Popen(command, cwd=repo, env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    stdout,stderr = proc.communicate(timeout=180)
    end = system_times()
    elapsed = (time.perf_counter()-start)*1000
    times = [FileTime() for _ in range(4)]
    cpu = dict(available=bool(k32.GetProcessTimes(wt.HANDLE(int(proc._handle)),
                               *(ct.byref(v) for v in times))))
    if cpu['available']:
        cpu.update(kernel_ms=ms(times[2]), user_ms=ms(times[3]))
    memory = Memory()
    memory.cb = ct.sizeof(memory)
    memory_ok = bool(psapi.GetProcessMemoryInfo(wt.HANDLE(int(proc._handle)),
                                              ct.byref(memory), ct.sizeof(memory)))
    ident = f'{tag}-{arm}-{operation}'
    (logs/(ident+'.stdout')).write_text(stdout)
    (logs/(ident+'.stderr')).write_text(stderr)
    assert proc.returncode == 0, (ident, proc.returncode, stderr)
    row = fields(stdout)
    assert (row['types'], row['objects'], row['links']) == (24027,137584,367144), ident
    if arm == 'A':
        assert row['pls08_candidate'] == 0
    payload = {n:sha(artifact/n) for n in names}
    if gold is None:
        gold = payload
    assert payload == gold, (ident, 'byte parity', payload, gold)
    if audit:
        digest = row['shm_audit_sha256']
        if shm is None:
            shm = digest
        assert digest == shm, (ident, 'SHM parity')
    row.update(arm=arm, operation=operation, tag=tag, command=command,
               process_wall_ms=elapsed, cpu=cpu,
               page_fault_count=int(memory.PageFaultCount) if memory_ok else None,
               system_busy_cpu_ms=(end[1]-begin[1])+(end[2]-begin[2])-(end[0]-begin[0]),
               artifact_sha256=payload)
    print(label, ident, row['total_ms'], 'ms', flush=True)
    return row

orders = list(itertools.permutations(('A','B','C')))
try:
    # A has no live-SHM digest hook. Load A's unchanged bytes using the common
    # modern auditor, then compare with modern PUBLISH SHM outside timing.
    checks.append(run('A','publish','pre'))
    checks.append(run('B','audit','pre-A-artifacts',True))
    for arm in ('B','C'):
        checks.append(run(arm,'publish','pre',True))
        checks.append(run(arm,'load','pre',True))
    for arm in ('C','B','A'):
        checks.append(run(arm,'publish','warm'))
    for series,operation,count in (('native','publish',blocks),
                                    ('profile','publish-timeline',profiles)):
        for block in range(count):
            for arm in orders[block % len(orders)]:
                row = run(arm,operation,f'{series}-{block:02}')
                row.update(series=series, block=block)
                samples.append(row)
            (dest/'samples.json').write_text(json.dumps(samples, indent=2))
    checks.append(run('A','publish','post'))
    checks.append(run('B','audit','post-A-artifacts',True))
    checks.append(run('C','publish','post',True))
    checks.append(run('C','audit','post',True))
finally:
    for name in names:
        shutil.copy2(backup/name, artifact/name)

rng = random.Random(26026)
summary = {}
for series in ('native','profile'):
    rows = [r for r in samples if r['series'] == series]
    if not rows:
        continue
    arms = {a: sorted([r for r in rows if r['arm']==a], key=lambda r:r['block']) for a in ('A','B','C')}
    medians = {a:{k:statistics.median(r[k] for r in v) for k in v[0]
                  if all(isinstance(r.get(k), (int,float)) for r in v)} for a,v in arms.items()}
    pairs = {}
    for a,b in (('A','B'),('A','C'),('B','C')):
        differences = [x['total_ms']-y['total_ms'] for x,y in zip(arms[a],arms[b])]
        boot = sorted(statistics.median(rng.choices(differences,k=len(differences))) for _ in range(20000))
        pairs[a+'-'+b] = dict(median_gain_ms=statistics.median(differences),
                              ci95_ms=[boot[500],boot[19499]], wins=sum(d>0 for d in differences),
                              blocks=len(differences))
    summary[series] = dict(medians=medians, paired=pairs)
(dest/'summary.json').write_text(json.dumps(summary, indent=2))
(dest/'checks.json').write_text(json.dumps(checks, indent=2))
assert all(sha(fixture/p)==h for p,h in inputs.items())
assert all(sha(artifact/n)==sha(backup/n) for n in names)
assert all(sha(Path(d['path']))==d['sha256'] for d in metadata['binaries'].values())
(dest/'verified.json').write_text(json.dumps(dict(input_unchanged=True, binary_unchanged=True,
    original_artifacts_restored=True, artifact_gold=gold, shm_from_common_loader=shm,
    historical_live_shm_digest_available=False, samples=len(samples), checks=len(checks)),indent=2))
print(json.dumps({s:v['paired'] for s,v in summary.items()},indent=2), flush=True)
