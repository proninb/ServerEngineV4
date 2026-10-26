"""Single C5 fast path; no queue/Graph/persistence algorithm changes."""
from pathlib import Path
import subprocess,sys
repo,out=map(lambda x:Path(x).resolve(),sys.argv[1:]);own=repo/'experiments/parallel-local-source-10-c7';parent=repo/'experiments/parallel-local-source-10-c3'
subprocess.run([sys.executable,str(repo/'experiments/parallel-local-source-10-c5/generate.py'),str(repo),str(out)],check=True)
def once(s,a,b):
 assert s.count(a)==1,(a,s.count(a));return s.replace(a,b)
s=(parent/'commit.hpp').read_text()
s=once(s,'namespace pls10c3 {','''namespace pls10c7 {
// One safety annotation on event-local scratch. The shared PLS02 endpoint
// layout is unchanged across parser/oracle translation units.
struct checked_endpoint:pls02::typed_endpoint {
 bool direct_domain_valid=false;
 checked_endpoint(cw::server::identity_ref who,cw::server::type_ref root,cw::server::type_ref final,pls02::range steps,pls02::origin at,bool valid)
  :pls02::typed_endpoint{who,root,final,steps,at},direct_domain_valid(valid){}
};
}
namespace pls10c3 {''')
s='#include "'+(own/'selector.hpp').as_posix()+'"\n'+s
s=once(s,'object_endpoint encode(const target& t)','template<class T>object_endpoint encode(const T& t)')
s=once(s,'    void declaration(identity_ref who)',(own/'view_body.inc').read_text()+'\n    void declaration(identity_ref who)')
s=once(s,'void write(const typed_endpoint& e,','void write(const pls10c7::checked_endpoint& e,')
s=once(s,'void bind(const typed_endpoint& lhs,const typed_endpoint& rhs,','void bind(const pls10c7::checked_endpoint& lhs,const pls10c7::checked_endpoint& rhs,')
s=once(s,'at(e.at);auto t=resolve(e,paths);const auto ep=encode(t);','at(e.at);with_endpoint(e,paths,false,[&](const auto& t){const auto ep=encode(t);')
s=once(s,'status(sources.add(current.file,source_data_ref::object(t.who)));++writes;','status(sources.add(current.file,source_data_ref::object(t.who)));++writes;});')
s=once(s,'at(lhs.at);const auto t=resolve(lhs,paths),s=resolve(rhs,paths,true);type_ref child;', 'at(lhs.at);with_endpoint(lhs,paths,false,[&](const auto& t){with_endpoint(rhs,paths,true,[&](const auto& s){type_ref child;')
s=once(s,'status(sources.add(current.file,source_data_ref::link(link)));++bindings;','status(sources.add(current.file,source_data_ref::link(link)));++bindings;});});')
(out/'commit.hpp').write_text(s)
s=(out/'ordered_binder.hpp').read_text().replace((parent/'commit.hpp').as_posix(),(out/'commit.hpp').as_posix())
s=once(s,'std::vector<typed_endpoint> endpoints;','std::vector<pls10c7::checked_endpoint> endpoints;')
s=once(s,'std::span<const endpoint_path_step> path,source_range at){','std::span<const endpoint_path_step> path,source_range at,bool direct_domain_valid=true){')
s=once(s,'{local->input.file,at}});return static_cast<id>(out.endpoints.size()-1);','{local->input.file,at},direct_domain_valid});return static_cast<id>(out.endpoints.size()-1);')
s=once(s,'source_typed_endpoint value;source_endpoint_resolver::failure error;','source_typed_endpoint value;source_endpoint_resolver::failure error;bool direct_domain_valid=true;')
s=once(s,'if(succeeded(status_))status_=resolver.append_member','if(succeeded(status_)){const auto* domain=G.find(record);if(!domain||domain->record_kind==graph_record_kind::union_type)direct_domain_valid=false;}\n                if(succeeded(status_))status_=resolver.append_member')
s=once(s,'owner,error);\n            }else{','owner,error);if(succeeded(status_)&&owner!=record)direct_domain_valid=false;\n            }else{')
s=once(s,'return save(who,value.object_type,value.type,value.path,request.at);','return save(who,value.object_type,value.type,value.path,request.at,direct_domain_valid);')
s=once(s,'save(target.who,target.root_type,array.child,path,target.at.source);','save(target.who,target.root_type,array.child,path,target.at.source,target.direct_domain_valid);')
(out/'ordered_binder.hpp').write_text(s)
b=(out/'publish_main.cpp').read_text();b='#include "'+(own/'selector.hpp').as_posix()+'"\n'+b
b=once(b,'pls10c3::print(std::cout);','pls10c7::print(std::cout);pls10c3::print(std::cout);');b=b.replace('PLS10C5.','PLS10C7.');(out/'publish_main.cpp').write_text(b)
