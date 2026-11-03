"""Observe remaining ordered C10 work; no algorithm or data-layout changes."""
from pathlib import Path
import re,subprocess,sys
repo,out=map(lambda p:Path(p).resolve(),sys.argv[1:]);own=Path(__file__).resolve().parent;parent=repo/'experiments/parallel-local-source-10-c3'
subprocess.run([sys.executable,str(repo/'experiments/parallel-local-source-10-c10/generate.py'),str(repo),str(out)],check=True)
def once(s,a,b):
 assert s.count(a)==1,(a,s.count(a));return s.replace(a,b)
include='#include "'+(own/'probe.hpp').as_posix()+'"\n'
for p in out.iterdir():
 if p.suffix in ('.cpp','.hpp'):
  s=p.read_text()
  for n in ('audit.hpp','coarse.hpp'):s=s.replace((repo/'experiments/parallel-local-source-10-c10'/n).as_posix(),(own/n).as_posix())
  p.write_text(s)
for folder in (out,out/'native'):
 p=folder/'ordered_binder.hpp';s=include+p.read_text()
 s=once(s,'for(const auto& e:local->events){','for(const auto& e:local->events){pls14::event_context event_context(pls14::observing()&&e.kind==event_kind::assign&&e.value<local->values.size()&&local->values[e.value].kind==value_kind::string);pls14::sample event_observer(pls14::event);')
 for anchor,metric in [('id endpoint_request(id n){','endpoint'),('string_id name(id n){','name'),('identity_ref symbol_entity(id symbol_id,identity_kind kind){','symbol'),('type_ref type(id n){','type'),('void declare(const event& e){','declare_event')]:
  s=once(s,anchor,anchor+'pls14::sample sample_observer(pls14::'+metric+');')
 s=once(s,'auto& r=name_ids.at(n);if(!r)', 'auto& r=name_ids.at(n);\n#ifdef PLS10_C9_AUDIT\nif(pls14::active&&!pls14::is_string){if(r)++pls14::name_hits;else ++pls14::name_misses;}\n#endif\nif(!r)')
 s=once(s,'auto& result=types.at(n);if(result)', 'auto& result=types.at(n);\n#ifdef PLS10_C9_AUDIT\nif(pls14::active&&!pls14::is_string){if(result)++pls14::type_hits;else ++pls14::type_misses;}\n#endif\nif(result)')
 s=once(s,'        auto base=scope_id(s.scope);', '\n#ifdef PLS10_C9_AUDIT\nif(pls14::active&&!pls14::is_string)pls14::symbol_components+=c.size();\n#endif\n        auto base=scope_id(s.scope);')
 s=once(s,'            const auto location=s.source();', '\n#ifdef PLS10_C9_AUDIT\nif(pls14::active&&!pls14::is_string){if(s.kind()==selector_kind::member)++pls14::member_selectors;else ++pls14::index_selectors;}\n#endif\n            const auto location=s.source();')
 for call in ('G.add_resolved_object(who,t,object)','G.add_resolved_object(who,t,object,flags,initial)','G.complete_resolved_extern_object(who,t,object,flags,initial)'):
  s=once(s,call,'pls14::observe(pls14::object,[&]{return '+call+';})')
 p.write_text(s)
 p=folder/'commit.hpp';s=include+p.read_text()
 for anchor,metric in [('target resolve(const typed_endpoint& e,const std::vector<endpoint_path_step>& paths,bool final=false){','temporal'),('target follow(target t,unsigned depth){','follow'),('template<class T>object_endpoint encode(const T& t){','encode'),('void warn(origin previous_origin){','warning'),('static server_status decide(void* p,std::uint32_t slot,bool,bool& accept)noexcept{','origin')]:
  s=once(s,anchor,anchor+'pls14::sample sample_observer(pls14::'+metric+');')
 for call,metric in [('G.intern_resolved_endpoint_path(t.root,t.steps,t.type,p)','path'),('G.apply_resolved_initialization(ep,t.type,value,decide,this,output)','slot'),('G.rebind_resolved_link(encode(s),encode(t),link,replaced)','link'),('sources.add_initialization(ep)','provenance'),('sources.add(current.file,source_data_ref::object(t.who))','provenance'),('sources.add(current.file,source_data_ref::object(who))','provenance'),('sources.add(current.file,source_data_ref::link(link))','provenance')]:
  s=once(s,call,'pls14::observe(pls14::'+metric+',[&]{return '+call+';})')
 # Link encoding belongs to the link expression scope above; it overlaps encode
 # and intern timers, rather than pretending to be an exclusive Graph duration.
 s=once(s,'if(link.value()>links.size())links.resize(link.value());','{pls14::sample origin_observer(pls14::origin);if(link.value()>links.size())links.resize(link.value());')
 s=once(s,'old={current,true};\n        status(pls14::observe','old={current,true};}\n        status(pls14::observe')
 p.write_text(s)
 p=folder/'publish_main.cpp';s=include+p.read_text();s=once(s,'pls10c10::print(std::cout);','pls10c10::print(std::cout);pls14::print(std::cout);');p.write_text(s)
 if folder==out/'native':
  s=(parent/'publish_source.hpp').read_text()
  s=re.sub(r'#include "([^\"]+)"',lambda m:'#include "'+(parent/m[1]).resolve().as_posix()+'"' if m[1].startswith('../') or m[1]=='stats.hpp' else m[0],s)
 else:s=(folder/'publish_source.hpp').read_text()
 s=include+s
 s=once(s,'using namespace cw::server;','using namespace cw::server;pls14::source_scope source_observer14;')
 s=once(s,'{std::unique_lock lock(mutex);cv.wait(lock,[&]{return slots','{pls14::coarse_scope wait_observer(pls14::wait);std::unique_lock lock(mutex);cv.wait(lock,[&]{return slots') if folder==out/'native' else once(s,'{pls10c9::scope waiting_observer','{pls14::coarse_scope wait_observer(pls14::wait);pls10c9::scope waiting_observer')
 s=once(s,'commit.apply(*value);','{pls14::coarse_scope apply_observer(pls14::apply);commit.apply(*value);}')
 s=once(s,'value.reset();','{pls14::coarse_scope cleanup_observer(pls14::cleanup);value.reset();}')
 for call in ('sources.begin_root(tasks[rank].file)','sources.end_root()'):
  s=once(s,call,'[&]{pls14::coarse_scope root_observer(pls14::roots);return '+call+';}()')
 (folder/'publish_source.hpp').write_text(s)
 p=folder/'publish_parser.cpp';s=p.read_text().replace((parent/'publish_source.hpp').as_posix(),(folder/'publish_source.hpp').as_posix());p.write_text(s)
