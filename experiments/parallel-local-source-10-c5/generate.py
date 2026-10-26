"""Audit whether project-visible streaming commit needs future declarations."""
from pathlib import Path
import subprocess,sys
repo,out=map(lambda p:Path(p).resolve(),sys.argv[1:]);parent=repo/'experiments/parallel-local-source-10-c3'
subprocess.run([sys.executable,str(parent/'generate.py'),str(repo),str(out)],check=True)
s=(out/'ordered_binder.hpp').read_text()
old='if(!declared.contains(who.value()))fail("Object needs a declaration or extern in this Source file",request.at);'
assert s.count(old)==1;s=s.replace(old,'')
s=s.replace('std::unordered_set<id> declared;','').replace('declared.clear();','').replace('declared.insert(who.value());','')
s=s.replace('Object must be declared before use (or extern)','Unknown or not-yet-declared project object')
(out/'ordered_binder.hpp').write_text(s)
b=(out/'publish_main.cpp').read_text().replace('PLS10C3.','PLS10C5.');(out/'publish_main.cpp').write_text(b)
