"""PLS04 early release + PLS03 flat; vary only write-head indexing."""
from pathlib import Path
import subprocess,sys
repo,out=map(lambda p:Path(p).resolve(),sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
subprocess.run([sys.executable,str(repo/'experiments/parallel-local-source-04/generate.py'),str(repo),str(out)],check=True)
def once(s,old,new):
 assert s.count(old)==1,(old,s.count(old));return s.replace(old,new)
s=(out/'aliases.hpp').read_text();s=once(s,'#include <numeric>','#include <numeric>\n#include "'+(repo/'experiments/parallel-local-source-06/heads.hpp').as_posix()+'"')
s=once(s,'void reduce_writes(){','void reduce_writes(){')
# Patch only the body of reduce_writes; binding heads remain unordered_map.
start=s.index('void reduce_writes(){');before=s[:start];s=s[start:]
s=once(s,'std::unordered_map<id,id> heads;','pls06::write_heads heads(out.cells.size());')
s=once(s,'const auto [it,inserted]=(pls03::count(pls03::heads_insert),heads.emplace(c,w));\n            if(!inserted){next[w]=it->second;it->second=w;}',
 'const auto previous=heads.exchange(c,w);if(previous!=none)next[w]=previous;')
s=once(s,'for(const auto& [c,head]:heads){','heads.each([&](id c,id head){')
s=once(s,'out.warnings.push_back({p.at,n.at,p.event,n.event,c,false});}}','out.warnings.push_back({p.at,n.at,p.event,n.event,c,false});}});pls06::stats.populated=out.values.size();')
(out/'aliases.hpp').write_text(before+s)
s=(out/'main.cpp').read_text();s=once(s,'pls04::write(std::getenv("PLS04_MEMORY_PATH"));','pls04::write(std::getenv("PLS04_MEMORY_PATH"));pls06::report(std::getenv("PLS06_HEADS_PATH"));')
(out/'main.cpp').write_text(s)
