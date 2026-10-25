"""Reuse PLS03 exactly, adding only file-local lifetime and observation."""
from pathlib import Path
import subprocess,sys
repo,out=map(lambda p:Path(p).resolve(),sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
subprocess.run([sys.executable,str(repo/'experiments/parallel-local-source-03/generate.py'),str(repo),str(out)],check=True)
def once(s,old,new):
 assert s.count(old)==1,(old,s.count(old));return s.replace(old,new)
base=repo/'experiments/parallel-local-source-02'
memory=(repo/'experiments/parallel-local-source-04/memory.hpp').as_posix()
s=(base/'bind.hpp').read_text();s=once(s,'#pragma once','#pragma once\n#include "'+memory+'"')
# Preserve all includes when relocating a generated copy.
import re
s=re.sub(r'#include "([^"]+)"',lambda m:m[0] if Path(m[1]).is_absolute() else '#include "'+(base/m[1]).resolve().as_posix()+'"',s)
s=once(s,'run(const std::vector<model>& inputs)','run(std::vector<model>& inputs)')
s=once(s,'std::vector<const model*> ordered;for(const auto& m:inputs)','std::vector<model*> ordered;for(auto& m:inputs)')
s=once(s,'pls03::lifetime_file(*local,out.bytes());','pls03::lifetime_file(*local,out.bytes());')
s=once(s,'#endif\n        }\n        const extern_state* earliest', '#endif\n            pls04::release_file(*ordered[i]);local=nullptr;\n        }\n        const extern_state* earliest')
(out/'bind.hpp').write_text(s)
s=(out/'aliases.hpp').read_text().replace((base/'bind.hpp').as_posix(),(out/'bind.hpp').as_posix());(out/'aliases.hpp').write_text(s)
s=(out/'main.cpp').read_text();s=once(s,'const auto lifecycle_start=clock_type::now();','pls04::guard memory_guard;pls04::start();const auto lifecycle_start=clock_type::now();')
s=once(s,'const auto link_start=clock_type::now();','pls04::phase_begin(pls04::linking);const auto link_start=clock_type::now();')
s=once(s,'locals.clear();locals.shrink_to_fit();','pls04::phase_begin(pls04::local_release);const auto local_release_start=clock_type::now();locals.clear();locals.shrink_to_fit();pls04::tail_destruction_ms=ms(local_release_start);')
s=once(s,'const auto oracle_start=clock_type::now();','pls04::pause();const auto oracle_start=clock_type::now();')
s=once(s,'pls03::begin(bound);','pls04::phase_begin(pls04::aliases);pls03::begin(bound);')
s=once(s,'pls03::finish(bound,alias_ms);','pls04::pause();pls03::finish(bound,alias_ms);')
s=once(s,'const auto consumer_start=clock_type::now();','pls04::phase_begin(pls04::consumer);const auto consumer_start=clock_type::now();')
s=once(s,'std::ofstream report(output);check(bool(report),','pls04::pause();std::ofstream report(output);check(bool(report),')
s=once(s,'const auto release_start=clock_type::now();bound={};','pls04::phase_begin(pls04::ir_release);const auto release_start=clock_type::now();bound={};')
s=once(s,'const auto release_ms=ms(release_start);','const auto release_ms=ms(release_start);pls04::finish();pls04::write(std::getenv("PLS04_MEMORY_PATH"));')
(out/'main.cpp').write_text(s)
