"""Summarize execution-mode crossover and retain current build provenance."""
from pathlib import Path
import hashlib, json, random, statistics, subprocess, sys, winreg

repo = Path(__file__).resolve().parents[2]
out = Path(sys.argv[1]).resolve()
assert out.is_relative_to(repo/'build')
rows = [json.loads(p.read_text()) for p in (out/'token-crossover').glob('*.json')
        if p.name != 'summary.json']
rng = random.Random(26026)
summary = {}
for operation in ('publish','publish-timeline'):
    arms = {a:sorted([r for r in rows if r['operation']==operation and r['label']==a],
                    key=lambda r:r['pair']) for a in ('restricted','unrestricted')}
    medians = {a:{k:statistics.median(r[k] for r in v) for k in v[0]
                  if all(isinstance(r.get(k),(int,float)) for r in v)} for a,v in arms.items()}
    gains = [a['total_ms']-b['total_ms'] for a,b in zip(arms['restricted'],arms['unrestricted'])]
    assert len(arms['restricted'])==len(arms['unrestricted'])
    boot = sorted(statistics.median(rng.choices(gains,k=len(gains))) for _ in range(20000))
    summary[operation] = dict(medians=medians, paired_execution_mode_cost_ms=statistics.median(gains),
        ci95_ms=[boot[500],boot[19499]], unrestricted_wins=sum(g>0 for g in gains),pairs=len(gains))
(out/'token-crossover/summary.json').write_text(json.dumps(summary,indent=2))

paths = ['build/parallel-local-source-10-c7/CMakeCache.txt',
         'build/parallel-local-source-10-c7/PublishTypedView10C7.vcxproj',
         'build/parallel-local-source-10-c7/generated/publish_main.cpp',
         'build/parallel-local-source-10-c7/generated/publish_parser.cpp',
         'build/parallel-local-source-10-c4/CMakeCache.txt',
         'build/parallel-local-source-10-c4/base/PLS08Core.vcxproj',
         'build/parallel-local-source-10-c4/base/Release/PLS08Core.lib',
         'build/publish-fast-acquire-02/build/CMakeCache.txt',
         'build/publish-fast-acquire-02/build/ServerEngineV4PublishBenchmark.vcxproj']
provenance = {}
for name in paths:
    path = repo/name
    with path.open('rb') as stream:
        digest = hashlib.file_digest(stream,'sha256').hexdigest()
    provenance[name] = dict(sha256=digest,bytes=path.stat().st_size,
                           mtime_ns=path.stat().st_mtime_ns)
(out/'current-build-provenance.json').write_text(json.dumps(dict(files=provenance,
    caveat='Current files only; not proof of the exact historical link inputs.'),indent=2))
with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,r'HARDWARE\DESCRIPTION\System\CentralProcessor\0') as key:
    host = dict(cpu=winreg.QueryValueEx(key,'ProcessorNameString')[0].strip(),
                registry_mhz=winreg.QueryValueEx(key,'~MHz')[0],
                caveat='Registry MHz is not sampled dynamic frequency during timed runs.')
for cmd,name in ((['powercfg','/getactivescheme'],'power-plan.txt'),
                 (['powercfg','/query','SCHEME_CURRENT','SUB_PROCESSOR'],'power-processor-settings.txt')):
    result = subprocess.run(cmd,capture_output=True,text=True,errors='replace')
    (out/name).write_text(result.stdout+'\n'+result.stderr)
(out/'host.json').write_text(json.dumps(host,indent=2))
verified = json.loads((out/'unrestricted/verified.json').read_text())
artifact = repo/'build/parallel-local-source-10-c9/ab-01/fixture/.serverengine/project.json'
for name,digest in verified['artifact_gold'].items():
    with (artifact/name).open('rb') as stream:
        assert hashlib.file_digest(stream,'sha256').hexdigest()==digest
print(json.dumps(summary,indent=2))
