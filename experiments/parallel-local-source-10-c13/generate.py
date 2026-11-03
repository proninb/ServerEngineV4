"""One-entry string-path reuse on clean C10; no batching or new endpoint index."""
from pathlib import Path
import subprocess,sys
repo,out=map(lambda p:Path(p).resolve(),sys.argv[1:]);own=Path(__file__).resolve().parent
subprocess.run([sys.executable,str(repo/'experiments/parallel-local-source-10-c10/generate.py'),str(repo),str(out)],check=True)
def once(s,a,b):
 assert s.count(a)==1,(a,s.count(a));return s.replace(a,b)
for folder in (out,out/'native'):
 p=folder/'commit.hpp';s='#include "'+(own/'reuse.hpp').as_posix()+'"\n'+p.read_text()
 s=once(s,'    origin current{};','    origin current{};\n    pls13::last_paths string_paths;')
 s=once(s,'    void declaration(identity_ref who)', '    [[nodiscard]] auto string_scope(std::uint64_t count,type_ref child){return string_paths.begin(count,child);}\n    void declaration(identity_ref who)')
 s=once(s,'else {endpoint_path_handle p;status(G.intern_resolved_endpoint_path(t.root,t.steps,t.type,p));ep=endpoint_ref::from_path(p);}',
 '''else {auto* cached=string_paths.lookup(t);endpoint_path_handle p;
            if(cached&&*cached){p=*cached;
#ifdef PLS13_AUDIT
                endpoint_path_record actual;const auto steps=G.endpoint_path_steps(p);
                if(!G.endpoint_path(p,actual)||actual.root_type!=t.root||actual.value_type!=t.type||steps.size()!=t.steps.size()||!std::equal(steps.begin(),steps.end(),t.steps.begin()))fail("C13 memo differs from resolved Graph path");
                pls13::count(pls13::audit.verified);
#endif
            }else {status(G.intern_resolved_endpoint_path(t.root,t.steps,t.type,p));if(cached)*cached=p;}
            ep=endpoint_ref::from_path(p);}''')
 p.write_text(s)
 p=folder/'ordered_binder.hpp';s=p.read_text();s=once(s,'// Copy the typed prefix once;', 'auto string_reuse=sink.string_scope(array.payload,array.child);\n            // Copy the typed prefix once;');p.write_text(s)
 p=folder/'publish_main.cpp';s='#include "'+(own/'reuse.hpp').as_posix()+'"\n'+p.read_text();s=once(s,'pls10c10::print(std::cout);','pls10c10::print(std::cout);pls13::print(std::cout);');p.write_text(s)
