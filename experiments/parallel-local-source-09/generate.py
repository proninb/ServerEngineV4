"""PLS09-A observation only. Reuse PLS08-C's algorithms/core unchanged."""
from pathlib import Path
import re,subprocess,sys
repo,out=map(lambda p:Path(p).resolve(),sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
subprocess.run([sys.executable,str(repo/'experiments/parallel-local-source-08/generate.py'),str(repo),str(out)],check=True)
def once(s,old,new):
 assert s.count(old)==1,(old,s.count(old));return s.replace(old,new)
base=repo/'experiments/parallel-local-source-03'
probe=(base/'probe.hpp').read_text()
probe=re.sub(r'#include "([^"]+)"',lambda m:'#include "'+(base/m[1]).resolve().as_posix()+'"',probe)
probe=once(probe,'header,catalog,references,','header,catalog,binding_copy,references,')
probe=once(probe,'write_targets,reduction,','write_targets,endpoint_release,reduction,')
probe=once(probe,'"binding_catalog","reference_resolution"','"binding_catalog","binding_catalog_copy","reference_resolution"')
probe=once(probe,'"write_targets_and_index","lww_reduction"','"write_targets_and_index","endpoint_release","lww_reduction"')
# Keep method clocks disabled: only coarse boundaries, scalar counters and
# optional append-only request descriptors. Reuse aggregation is after alias_ms.
probe=once(probe,'#ifdef PLS03_AUDIT\n    if(active!=off)stages[active].counts[m]+=n;', '#ifdef PLS09_DETAILS\n    if(active!=off)stages[active].counts[m]+=n;')
probe=once(probe,'#ifdef PLS03_AUDIT\n    pause p;requests.push_back', '#ifdef PLS09_DETAILS\n    pause p;requests.push_back')
probe=once(probe,'#ifdef PLS03_AUDIT\n    requests.reserve', '#ifdef PLS09_DETAILS\n    requests.reserve')
probe=once(probe,'#ifdef PLS03_AUDIT\n      <<"true"', '#ifdef PLS09_DETAILS\n      <<"true"')
probe=once(probe,'#ifdef PLS03_AUDIT\n    const auto less=', '#ifdef PLS09_DETAILS\n    const auto less=')
(out/'probe.hpp').write_text(probe)
flat=(base/'flat.hpp').read_text().replace('#include "probe.hpp"','#include "'+(out/'probe.hpp').as_posix()+'"');(out/'flat.hpp').write_text(flat)
for name in ('bind.hpp','aliases.hpp'):
 s=(out/name).read_text().replace((base/'probe.hpp').as_posix(),(out/'probe.hpp').as_posix()).replace((base/'flat.hpp').as_posix(),(out/'flat.hpp').as_posix())
 if name=='aliases.hpp':
  s=once(s,'bindings=direct;++out.link_rounds;', 'pls03::boundary(pls03::binding_copy);bindings=direct;pls03::boundary(pls03::catalog);++out.link_rounds;')
  s=once(s,'pls07::release_endpoints(model);', 'pls03::boundary(pls03::endpoint_release);pls07::release_endpoints(model);')
 (out/name).write_text(s)
hook=(repo/'experiments/parallel-local-source-08/publish_source.hpp').read_text()
origin=repo/'experiments/parallel-local-source-08'
hook=re.sub(r'#include "([^"]+)"',lambda m:m[0] if m[1]=='aliases.hpp' else '#include "'+(origin/m[1]).resolve().as_posix()+'"',hook)
hook=once(hook,'at=clock::now();auto final=pls02::alias_linker(G,bound).run();alias_ms=elapsed(at);', '''pls03::begin(bound);at=clock::now();auto final=pls02::alias_linker(G,bound).run();alias_ms=elapsed(at);
  // Temporary alias_linker has already been destroyed: release phase includes
  // scratch/index destruction. Export is outside the measured alias interval,
  // before ownership moves paths needed by cold reuse aggregation.
  pls03::finish(bound,alias_ms);''')
(out/'publish_source.hpp').write_text(hook)
p=(out/'publish_parser.cpp').read_text().replace((origin/'publish_source.hpp').as_posix(),(out/'publish_source.hpp').as_posix());(out/'publish_parser.cpp').write_text(p)
