"""Copy current production sources into a new diagnostic workspace."""
from pathlib import Path
import hashlib,json,shutil,subprocess,sys
repo=Path(__file__).resolve().parents[2];out=Path(sys.argv[1]).resolve()
assert out.is_relative_to(repo/'build') and not out.exists()
root=out/'source';root.mkdir(parents=True)
for name in ('server_engine','benchmarks','tests'):shutil.copytree(repo/name,root/name)
shutil.copy2(repo/'CMakeLists.txt',root/'CMakeLists.txt')
(out/'source-manifest.json').write_text(json.dumps({p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(root.rglob('*')) if p.is_file()},indent=2))
(out/'snapshot.json').write_text(json.dumps(dict(git_head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip(),
    description='Unmodified current production V2, Fast Reader and Batch ON; optimized matching-symbol diagnostic build.'),indent=2))
(out/'capture').mkdir()
print(root)
