"""Observation only: C5 syntax/queue/Graph algorithms are retained verbatim."""
from pathlib import Path
import subprocess,sys
repo,out=map(lambda p:Path(p).resolve(),sys.argv[1:]);own=repo/'experiments/parallel-local-source-10-c6';parent=repo/'experiments/parallel-local-source-10-c3'
subprocess.run([sys.executable,str(repo/'experiments/parallel-local-source-10-c5/generate.py'),str(repo),str(out)],check=True)
def once(s,a,b):
 assert s.count(a)==1,(a,s.count(a));return s.replace(a,b)
probe='#include "'+(own/'probe.hpp').as_posix()+'"\n'
def scope(s,signature,tag):
 a=s.index(signature);b=s.index('{',a)+1;return s[:b]+f' pls10c6::frame observation(pls10c6::{tag}); '+s[b:]
s=(parent/'commit.hpp').read_text();s=probe+s
# Include generated binder through this header without creating a second model.
for signature,tag in [('object_endpoint encode(', 'endpoint_encode'),('target resolve(', 'typed_resolve'),('target follow(', 'reference_follow'),('void warn(', 'warning'),('static server_status decide(', 'origin')]:s=scope(s,signature,tag)
s=scope(s,'void bind(', 'binding_request')
s=once(s,'type_ref step(type_ref t,endpoint_path_step s){','type_ref step(type_ref t,endpoint_path_step s){pls10c6::step();')
s=once(s,'target t{e.who,e.root_type,e.root_type,{}};','pls10c6::target();target t{e.who,e.root_type,e.root_type,{}};')
s=once(s,'target t{ep.object,root(ep.object),{}, {}};','pls10c6::target();target t{ep.object,root(ep.object),{}, {}};')
s=once(s,'if(slot>=self.values.size())self.values.resize(std::size_t(slot)+1);','if(slot>=self.values.size()){pls10c6::frame growth(pls10c6::origin_reserve,true,slot>=self.values.capacity());self.values.resize(std::size_t(slot)+1);}')
s=once(s,'auto& old=self.values[slot];','auto& old=self.values[slot];pls10c6::hit(pls10c6::origin,old.present);')
# Fine provenance is separate from temporal resolution and physical Graph writes.
for call in ['sources.add(current.file,source_data_ref::object(who))','sources.add(current.file,source_data_ref::object(t.who))','sources.add_initialization(ep)','sources.add(current.file,source_data_ref::link(link))']:
 s=once(s,call,'pls10c6::measured(pls10c6::provenance,[&]{return '+call+';})')
s=once(s,'if(link.value()>links.size())links.resize(link.value());auto& old=links[link.value()-1];if(old.present)warn(old.at);old={current,true};',
 '''{pls10c6::frame origins(pls10c6::link_origin);if(link.value()>links.size()){pls10c6::frame growth(pls10c6::origin_reserve,true,link.value()>links.capacity());links.resize(link.value());}auto& old=links[link.value()-1];pls10c6::hit(pls10c6::link_origin,old.present);if(old.present)warn(old.at);old={current,true};}''')
(out/'commit.hpp').write_text(s)
s=(out/'ordered_binder.hpp').read_text().replace((parent/'commit.hpp').as_posix(),(out/'commit.hpp').as_posix())
for signature,tag in [('identity_ref symbol_entity(', 'who_bind'),('id endpoint_request(', 'typed_endpoint'),('type_ref type(', 'type_request'),('construction_value scalar(', 'scalar_value'),('void append_write(', 'write_request')]:s=scope(s,signature,tag)
s=once(s,'if(!succeeded(resolver.begin_endpoint(who,value,error)))','const auto result=pls10c6::measured(pls10c6::object_lookup,[&]{return resolver.begin_endpoint(who,value,error);});pls10c6::hit(pls10c6::object_lookup,succeeded(result));if(!succeeded(result))')
s=once(s,'for(const auto& e:local->events){current=', 'for(const auto& e:local->events){pls10c6::frame event_observation(e.kind==event_kind::declare?pls10c6::declaration_event:e.kind==event_kind::assign?pls10c6::assignment_event:pls10c6::namespace_event);current=')
(out/'ordered_binder.hpp').write_text(s)
s=(parent/'publish_source.hpp').read_text();s=probe+s
s=once(s,' try {',' try {pls10c6::observation observing;pls10c6::coarse source_body(pls10c6::body);')
s=once(s,'pls02::ordered_committer commit(', 'pls10c6::measured_owner<pls02::ordered_committer> commit(')
s=once(s,'{std::unique_lock lock(mutex);cv.wait(lock,[&]{return slots[rank%slots.size()].ready;});','{pls10c6::coarse wait(pls10c6::queue_wait);std::unique_lock lock(mutex);pls10c6::hit(pls10c6::queue_wait,slots[rank%slots.size()].ready);cv.wait(lock,[&]{return slots[rank%slots.size()].ready;});')
s=once(s,'if(!succeeded(sources.begin_root(tasks[rank].file)))','if(!succeeded(pls10c6::measured_exact(pls10c6::root_provenance,[&]{return sources.begin_root(tasks[rank].file);})))')
s=once(s,'commit.apply(*value);','{pls10c6::coarse apply(pls10c6::apply_file);commit.apply(*value);}')
s=once(s,'if(!succeeded(sources.end_root()))','if(!succeeded(pls10c6::measured_exact(pls10c6::root_provenance,[&]{return sources.end_root();})))')
s=once(s,'value.reset();','{pls10c6::coarse cleanup(pls10c6::model_cleanup);value.reset();}')
(out/'profile_source.hpp').write_text(s)
s=(out/'publish_parser.cpp').read_text().replace((parent/'publish_source.hpp').as_posix(),(out/'profile_source.hpp').as_posix());(out/'publish_parser.cpp').write_text(s)
s=(out/'publish_main.cpp').read_text();s=probe+s;s=once(s,'pls10c3::print(std::cout);','pls10c3::print(std::cout);pls10c6::dump();');s=s.replace('PLS10C5.','PLS10C6.');(out/'publish_main.cpp').write_text(s)
# Diagnostic copy only. The normal source/library and public ABI are untouched.
s=probe+(repo/'server_engine/project/graph/graph.cpp').read_text()
def segment(s,signature,change):
 start=s.index(signature);opening=s.index('{',start);end=s.index('\n}',opening)+2
 return s[:start]+change(s[start:end])+s[end:]
def lookup(part,kind,result_type):
 opening=part.index('{');body=part[opening+1:part.rfind('}')]
 return part[:opening+1]+f'pls10c6::frame observation(pls10c6::{kind});auto value=([&]() -> {result_type} {{'+body+f'}})();pls10c6::hit(pls10c6::{kind},bool(value));observation.outcome(bool(value));return value;}}'
s=segment(s,'std::uint32_t graph::find_initialization_position(',lambda p:lookup(p,'value_lookup','std::uint32_t'))
s=segment(s,'link_handle graph::find_link_target(',lambda p:lookup(p,'link_lookup','link_handle'))
for signature,tag in [('server_status graph::apply_resolved_initialization(', 'value_api'),('server_status graph::rebind_resolved_link(', 'link_api'),('server_status graph::add_resolved_link(', 'link_insert')]:s=scope(s,signature,tag)
def apply(p):
 p=once(p,'if (accept) object_initializations[existing - 1].value = value;','if (accept) {pls10c6::frame mutation(pls10c6::value_mutation);pls10c6::hit(pls10c6::value_mutation,true);object_initializations[existing - 1].value = value;}')
 a=p.index('    object_initializations.push_back({target, value});');b=p.index('    output.slot = slot;',a)
 p=p[:a]+'    {pls10c6::frame mutation(pls10c6::value_mutation);pls10c6::hit(pls10c6::value_mutation,false);\n'+p[a:b]+'    }\n'+p[b:]
 p=once(p,'if (object_initializations.size() == object_initializations.capacity())\n            object_initializations.reserve', 'if (object_initializations.size() == object_initializations.capacity()){\n            pls10c6::frame growth(pls10c6::value_reserve,true);object_initializations.reserve')
 p=once(p,'object_initializations.capacity() * 2)));','object_initializations.capacity() * 2)));}')
 return p
s=segment(s,'server_status graph::apply_resolved_initialization(',apply)
s=once(s,'try { links.push_back({source, target}); }','try {pls10c6::frame growth(pls10c6::link_reserve,true,links.size()==links.capacity()); links.push_back({source, target}); }')
# Both compile-time link-index variants keep their existing algorithms.
start=0
for signature,tag in [('server_status graph::ensure_initialization_target_index_capacity(', 'value_rehash'),('server_status graph::ensure_link_target_index_capacity(', 'link_rehash')]:
 cursor=0
 while True:
  a=s.find(signature,cursor)
  if a<0:break
  end=s.index('\n}',s.index('{',a))+2;part=s[a:end]
  needle='    const auto reference_capacity' if '    const auto reference_capacity' in part else '    const auto capacity'
  part=once(part,needle,f'    pls10c6::frame rehash(pls10c6::{tag},true);\n'+needle)
  s=s[:a]+part+s[end:];cursor=a+len(part)
(out/'profile_graph.cpp').write_text(s)
