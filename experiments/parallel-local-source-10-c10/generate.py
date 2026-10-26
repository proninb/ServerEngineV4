"""C7 scratch-lifetime candidate; independent count-only C9-derived audit."""
from pathlib import Path
import subprocess,sys
repo,out=map(lambda p:Path(p).resolve(),sys.argv[1:]);own=Path(__file__).resolve().parent
subprocess.run([sys.executable,str(repo/'experiments/parallel-local-source-10-c9/generate.py'),str(repo),str(out)],check=True)
def once(s,a,b):
 assert s.count(a)==1,(a,s.count(a))
 return s.replace(a,b)
for p in out.iterdir():
 if p.suffix in ('.cpp','.hpp'):p.write_text(p.read_text().replace((repo/'experiments/parallel-local-source-10-c9').as_posix(),own.as_posix()))
guard=lambda group:'\n#ifdef PLS10_C9_AUDIT\npls10c9audit::guard allocation_group('+str(group)+');\n#endif\n'
p=out/'ordered_binder.hpp';s=p.read_text()
s=once(s,'id endpoint_request(id n){','id endpoint_request(id n){'+guard(4))
s=once(s,'if(v.kind==value_kind::string){','if(v.kind==value_kind::string){'+guard(5));p.write_text(s)
p=out/'commit.hpp';s=p.read_text();s=once(s,'        at(e.at);with_endpoint','        '+guard(6)+'at(e.at);with_endpoint');s=once(s,'        at(lhs.at);with_endpoint','        '+guard(6)+'at(lhs.at);with_endpoint');p.write_text(s)
# Native path has no C9 counters, clocks, generated Graph or allocation override.
native=out/'native'
subprocess.run([sys.executable,str(repo/'experiments/parallel-local-source-10-c7/generate.py'),str(repo),str(native)],check=True)
for folder in (out,native):
 p=folder/'ordered_binder.hpp';s=p.read_text()
 s='#include "'+(own/'selector.hpp').as_posix()+'"\n'+s
 s=once(s,'    order current{};','    order current{};\n    source_typed_endpoint endpoint_scratch;')
 s=once(s,'source_typed_endpoint value;source_endpoint_resolver::failure error;',
 '''// begin_endpoint resets type, object_type and path length. save() copies
        // the path into out.paths before any subsequent endpoint request.
        source_typed_endpoint temporary;
        auto& value=pls10c10::reuse?endpoint_scratch:temporary;
        source_endpoint_resolver::failure error;''')
 p.write_text(s)
 p=folder/'publish_main.cpp';s='#include "'+(own/'selector.hpp').as_posix()+'"\n'+p.read_text();s=once(s,'pls10c3::print(std::cout);','pls10c3::print(std::cout);pls10c10::print(std::cout);');p.write_text(s)
