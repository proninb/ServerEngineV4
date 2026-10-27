"""Freeze production inputs and disposable fixtures before OFF/GENERATE/USE."""
import hashlib,json,shutil,subprocess,sys
from pathlib import Path
repo=Path(__file__).resolve().parents[2];out=Path(sys.argv[1]).resolve()
assert out.is_relative_to(repo/'build');source=out/'source';assert not source.exists();source.mkdir(parents=True)
shutil.copy2(repo/'CMakeLists.txt',source/'CMakeLists.txt')
for name in ('server_engine','tests'):shutil.copytree(repo/name,source/name)
fixtures=out/'fixtures';fixtures.mkdir()
for name,original in [('unit',repo/'build/parallel-local-source-08/publish-c-final/fixture'),('simple',repo/'build/publish-subsecond-01/simple'),('mixed',repo/'build/publish-subsecond-01/mixed')]:
 shutil.copytree(original,fixtures/name,ignore=shutil.ignore_patterns('.serverengine','__pycache__','*.exe','*.pdb'))
smoke=fixtures/'lifecycle';smoke.mkdir()
(smoke/'types.h').write_text('enum class Mode { Off=0, On=2 }; struct I { int x=5; }; struct T { I child; I& ref; char name[8]; Mode mode; T():ref(child){} };')
(smoke/'source.cpp').write_text('T t; I b; t.ref=b; b.x=7; t.name="abc"; t.mode=Mode::On;')
(smoke/'project.json').write_text(json.dumps(dict(version=1,name='ProductionPGOLifecycle',project=[dict(name='types',type='header',path='types.h'),dict(name='source',type='source',path='source.cpp')],preprocessor=dict(predefines=[]))))
legacy=fixtures/'legacy';legacy.mkdir()
(legacy/'types.h').write_text('struct I { int x; };')
(legacy/'source.cpp').write_text('I a; a.x=7;')
(legacy/'project.json').write_text(json.dumps(dict(version=1,name='ProductionPGOLegacy',project=[dict(name='types',type='header',path='types.h'),dict(name='source',type='source',path='source.cpp')],preprocessor=dict(predefines=[]))))
def inventory(root):return {p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(root.rglob('*')) if p.is_file()}
manifest=dict(head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip(),source=inventory(source),fixtures={name:inventory(p) for name,p in ((p.name,p) for p in fixtures.iterdir())})
(out/'freeze.json').write_text(json.dumps(manifest,indent=2))
(out/'source.patch').write_bytes(subprocess.check_output(['git','diff','--binary','--','CMakeLists.txt','server_engine'],cwd=repo))
print('Frozen production source files:',len(manifest['source']),'fixtures:',list(manifest['fixtures']))
