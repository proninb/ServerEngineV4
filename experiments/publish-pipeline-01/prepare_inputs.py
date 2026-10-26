"""Freeze binary and hash self-contained, read-only common input fixtures."""
from pathlib import Path
import hashlib,json,os,platform,shutil,sys,winreg
repo=Path(__file__).resolve().parents[2];out=Path(sys.argv[1]).resolve();assert out.is_relative_to(repo/'build')
exe=out/'build/Release/ServerEngineV4PublishBenchmark.exe';target=out/'binaries/PublishPipeline01.exe';target.parent.mkdir(exist_ok=True)
def sha(path):
 with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
if target.exists():assert sha(exe)==sha(target),'frozen binary differs; use a new experiment directory'
else:shutil.copy2(exe,target)
metadata=dict(sha256=sha(target),configuration='Release + IPO, MSVC 19.51, x64',arms=['baseline','serial','overlap'])
(out/'binary.json').write_text(json.dumps(metadata,indent=2))
roots={'synthetic':Path(sys.argv[2]).resolve() if len(sys.argv)>2 else repo/'build/direct-g-mmap-ab-01/corpora/synthetic',
       'unitproxl':Path(sys.argv[3]).resolve() if len(sys.argv)>3 else repo/'build/direct-g-mmap-ab-01/corpora/unitproxl'}
result={}
for corpus,root in roots.items():
 root=root.resolve();project=root/'project.json';config=json.loads(project.read_text(encoding='utf-8-sig'))
 def check(value):
  if isinstance(value,dict):
   for key,item in value.items():
    if key=='path' and isinstance(item,str):
     path=(root/Path(item)).resolve();assert path.is_relative_to(root),(corpus,'fixture must be self-contained',path)
    else:check(item)
  elif isinstance(value,list):
   for item in value:check(item)
 check(config)
 manifest={}
 for path in sorted(root.rglob('*')):
  if not path.is_file() or '.serverengine' in path.relative_to(root).parts:continue
  assert path.resolve().is_relative_to(root)
  manifest[path.relative_to(root).as_posix()]=sha(path)
 result[corpus]=dict(root=str(root),project=str(project),expected_types=10001 if corpus=='synthetic' else 24027,manifest=manifest)
(out/'input-manifest.json').write_text(json.dumps(result,indent=2))
with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,r'HARDWARE\DESCRIPTION\System\CentralProcessor\0') as key:
 cpu=winreg.QueryValueEx(key,'ProcessorNameString')[0].strip()
(out/'host.json').write_text(json.dumps(dict(os=platform.platform(),cpu=cpu,logical_processors=os.cpu_count()),indent=2))
print('Frozen binary and input manifests ready.')
