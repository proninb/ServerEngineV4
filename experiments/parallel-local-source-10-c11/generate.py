"""Observe C10 string lowering; do not change batching or algorithms."""
from pathlib import Path
import subprocess,sys
repo,out=map(lambda p:Path(p).resolve(),sys.argv[1:]);own=Path(__file__).resolve().parent
subprocess.run([sys.executable,str(repo/'experiments/parallel-local-source-10-c10/generate.py'),str(repo),str(out)],check=True)
def once(s,a,b):
 assert s.count(a)==1,(a,s.count(a));return s.replace(a,b)
for p in out.iterdir():
 if p.suffix in ('.cpp','.hpp'):
  s=p.read_text()
  for name in ('audit.hpp','coarse.hpp'):s=s.replace((repo/'experiments/parallel-local-source-10-c10'/name).as_posix(),(own/name).as_posix())
  p.write_text(s)
include='#include "'+(own/'probe.hpp').as_posix()+'"\n'
for folder in (out,out/'native'):
 p=folder/'ordered_binder.hpp';s=include+p.read_text()
 s=once(s,'if(v.bytes.count+1>array.payload)', 'pls11::string_scope string_observer(v.bytes.count,array.payload);\n            if(v.bytes.count+1>array.payload)')
 s=once(s,'for(std::uint64_t i=0;i<array.payload;++i){path.back().value=i;', 'for(std::uint64_t i=0;i<array.payload;++i){pls11::sample element_observer(pls11::element);path.back().value=i;');p.write_text(s)
 p=folder/'commit.hpp';s=include+p.read_text()
 for original,part in [('G.intern_resolved_endpoint_path(t.root,t.steps,t.type,p)','path'),('G.apply_resolved_initialization(ep,t.type,value,decide,this,output)','slot'),('sources.add_initialization(ep)','provenance'),('sources.add(current.file,source_data_ref::object(t.who))','provenance')]:
  s=once(s,original,'pls11::observe(pls11::'+part+',[&]{return '+original+';})')
 p.write_text(s)
 p=folder/'publish_main.cpp';s=include+p.read_text();s=once(s,'pls10c10::print(std::cout);','pls10c10::print(std::cout);pls11::print(std::cout);');p.write_text(s)
p=out/'audit_graph.cpp';s=include+p.read_text()
s=once(s,'pls10c9audit::count(pls10c9audit::path);','pls10c9audit::count(pls10c9audit::path);pls11::path_audit path_observer(*this,output,steps.size());')
s=once(s,'pls10c9audit::count(pls10c9audit::slot_apply);','pls10c9audit::count(pls10c9audit::slot_apply);pls11::slot_audit slot_observer(output);');p.write_text(s)
