"""Generate an observed PLS02, preserving algorithms and original lifetimes."""
from pathlib import Path
import re,sys
repo,out=map(lambda p:Path(p).resolve(),sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
base=repo/'experiments/parallel-local-source-02'
def once(s,old,new):
 assert s.count(old)==1,(old,s.count(old));return s.replace(old,new)
s=(base/'aliases.hpp').read_text()
s=once(s,'#include "bind.hpp"','#include "'+(base/'bind.hpp').as_posix()+'"\n#include "'+(repo/'experiments/parallel-local-source-03/probe.hpp').as_posix()+'"\n#include "'+(repo/'experiments/parallel-local-source-03/flat.hpp').as_posix()+'"')
s=once(s,'std::uint64_t h=k.who.value();for(', 'pls03::count(pls03::hash_calls);\n        std::uint64_t h=k.who.value();for(')
s=once(s,'h=(h^static_cast<std::uint64_t>(s.kind))','pls03::count(pls03::hash_steps);h=(h^static_cast<std::uint64_t>(s.kind))')
s=once(s,'return a.who==b.who && a.steps.count==b.steps.count && std::equal(paths->begin()+a.steps.begin,paths->begin()+a.steps.begin+a.steps.count,paths->begin()+b.steps.begin);',
'''pls03::count(pls03::equal_calls);
#ifdef PLS03_AUDIT
        if(a.who!=b.who || a.steps.count!=b.steps.count)return false;
        for(std::uint32_t i=0;i<a.steps.count;++i){pls03::count(pls03::equal_steps);if((*paths)[a.steps.begin+i]!=(*paths)[b.steps.begin+i])return false;}return true;
#else
        return a.who==b.who && a.steps.count==b.steps.count && std::equal(paths->begin()+a.steps.begin,paths->begin()+a.steps.begin+a.steps.count,paths->begin()+b.steps.begin);
#endif''')
s=once(s,'type_ref step(type_ref t,const endpoint_path_step& s) const {','type_ref step(type_ref t,const endpoint_path_step& s) const {\n        pls03::sample sample(pls03::step_method);pls03::count(pls03::path_walk_steps);')
s=once(s,'id intern(identity_ref who,type_ref t,range r){','id intern(identity_ref who,type_ref t,range r){pls03::sample sample(pls03::intern_method);pls03::count(pls03::intern_range);')
s=once(s,'id intern(identity_ref who,type_ref t,const std::vector<endpoint_path_step>& pp){','id intern(identity_ref who,type_ref t,const std::vector<endpoint_path_step>& pp){\n        pls03::sample sample(pls03::vector_method);pls03::count(pls03::intern_vector);')
s=s.replace('cells.find(key)','(pls03::count(pls03::cell_find),cells.find(key))').replace('cells.emplace(key,n)','(pls03::count(pls03::cell_insert),cells.emplace(key,n))')
s=once(s,'for(const auto& s:path(r))if(s.kind==endpoint_path_step_kind::dereference)return true;',
 'for(const auto& s:path(r)){pls03::count(pls03::deref_scan_steps);if(s.kind==endpoint_path_step_kind::dereference)return true;}')
s=once(s,'id canonical(const typed_endpoint& e,bool follow_final,unsigned depth=0){',
 'id canonical(const typed_endpoint& e,bool follow_final,unsigned depth=0){\n        pls03::sample sample(pls03::canonical_method);pls03::count(pls03::canonical_calls);pls03::canonical_request(e,follow_final);')
s=once(s,'if(!has_dereference(e.steps)){','if(!has_dereference(e.steps)){pls03::count(pls03::canonical_direct);')
s=once(s,'auto who=e.who;auto type=e.root_type;','pls03::count(pls03::canonical_deref);auto who=e.who;auto type=e.root_type;')
s=once(s,'id follow(id c,unsigned depth){','id follow(id c,unsigned depth){\n        pls03::sample sample(pls03::follow_method);pls03::count(pls03::follow_calls);')
s=once(s,'if(const auto it=cache.find(c);it!=cache.end())return it->second;',
 'pls03::count(pls03::cache_lookups);if(const auto it=cache.find(c);it!=cache.end()){pls03::count(pls03::follow_hits);return it->second;}pls03::count(pls03::follow_misses);')
s=s.replace('bindings.find(c)','(pls03::count(pls03::binding_lookups),bindings.find(c))')
# The previous spelling also matches the suffix of header_bindings.find(c).
s=s.replace('header_(pls03::count(pls03::binding_lookups),bindings.find(c))','(pls03::count(pls03::binding_lookups),header_bindings.find(c))')
s=s.replace('heads.emplace(c,b)','(pls03::count(pls03::heads_insert),heads.emplace(c,b))').replace('heads.emplace(c,w)','(pls03::count(pls03::heads_insert),heads.emplace(c,w))')
s=once(s,'into.find(cell)','(pls03::count(pls03::select_find),into.find(cell))')
s=once(s,'into[cell]=b;','(pls03::count(pls03::select_store),into[cell]=b);')
s=once(s,'cache[c]=source;','(pls03::count(pls03::cache_store),cache[c]=source);')
s=once(s,'visiting.insert(c)','(pls03::count(pls03::visiting_insert),visiting.insert(c))')
s=once(s,'visiting.erase(c);','(pls03::count(pls03::visiting_erase),visiting.erase(c));')
s=once(s,'bindings.at(c)','(pls03::count(pls03::binding_at),bindings.at(c))')
s=once(s,'void link_bindings(){','void link_bindings(){\n        pls03::boundary(pls03::header);')
s=once(s,'std::unordered_map<id,id> direct;','pls03::boundary(pls03::catalog);\n        std::unordered_map<id,id> direct;')
s=once(s,'cache.clear();visiting.clear();\n        std::vector<id> keys;', 'cache.clear();visiting.clear();\n        pls03::boundary(pls03::references);\n        std::vector<id> keys;')
s=once(s,'std::unordered_map<id,id> heads;std::vector<id> next(model.bindings.size(),none);',
 'pls03::boundary(pls03::binding_targets);\n        std::unordered_map<id,id> heads;std::vector<id> next(model.bindings.size(),none);')
s=once(s,'std::vector<id> hh;', 'pls03::boundary(pls03::binding_diagnostics);\n        std::vector<id> hh;')
s=once(s,'out.warnings.push_back({p.at,n.at,p.event,n.event,c,true});}}', 'out.warnings.push_back({p.at,n.at,p.event,n.event,c,true});}}\n        pls03::boundary(pls03::binding_cleanup);')
s=once(s,'void reduce_writes(){','void reduce_writes(){\n        pls03::boundary(pls03::write_targets);')
s=once(s,'std::vector<id> history;','pls03::boundary(pls03::reduction);\n        std::vector<id> history;')
s=once(s,'std::sort(out.values.begin(),out.values.end(),[](auto a,auto b){return a.cell<b.cell;});',
 'pls03::boundary(pls03::value_order);\n        std::sort(out.values.begin(),out.values.end(),[](auto a,auto b){return a.cell<b.cell;});\n        pls03::boundary(pls03::write_cleanup);')
s=once(s,'std::sort(out.warnings.begin(),out.warnings.end()', 'pls03::boundary(pls03::diagnostic_order);\n        std::sort(out.warnings.begin(),out.warnings.end()')
s=once(s,'return std::move(out);','pls03::boundary(pls03::release);return std::move(out);')
s=once(s,'result out;','result out;\n    pls03::flat_index flat;')
needle='id intern(identity_ref who,type_ref t,range r){pls03::sample sample(pls03::intern_method);pls03::count(pls03::intern_range);'
s=once(s,needle,needle+'\n        if(flat.enabled)return flat.intern(who,t,r,out.cells,model.paths);')
needle='const cell_key key{who,{begin,static_cast<id>(pp.size())}};'
s=once(s,needle,needle+'''\n        if(flat.enabled){const auto before=out.cells.size();const auto n=intern(who,t,key.steps);
            if(out.cells.size()==before)model.paths.resize(begin);return n;}''')
s=once(s,'pls03::boundary(pls03::release);return std::move(out);','pls03::flat_capacity_bytes=flat.bytes();pls03::boundary(pls03::release);return std::move(out);')
(out/'aliases.hpp').write_text(s)
s=(base/'main.cpp').read_text()
s=re.sub(r'#include "([^"]+)"',lambda m:'#include "'+((out/'aliases.hpp') if m[1]=='aliases.hpp' else (base/m[1]).resolve()).as_posix()+'"',s)
s=once(s,'const auto alias_start=clock_type::now();','pls03::begin(bound);const auto alias_start=clock_type::now();')
s=once(s,'const auto alias_ms=ms(alias_start);','const auto alias_ms=ms(alias_start);pls03::finish(bound,alias_ms);')
# Measured lifecycle is ready input -> aliases + fingerprint consumption + IR
# destruction. Oracle, observer summary and report IO are explicitly excluded.
s=once(s,'std::vector<model> locals(tasks.size());','const auto lifecycle_start=clock_type::now();std::vector<model> locals(tasks.size());')
s=once(s,'bool oracle_checked=false;if(!oracle.empty()){','const auto oracle_start=clock_type::now();bool oracle_checked=false;if(!oracle.empty()){')
s=once(s,'oracle_checked=true;}','oracle_checked=true;}const double oracle_ms=ms(oracle_start);')
s=once(s,'const auto alias_ms=ms(alias_start);pls03::finish(bound,alias_ms);',
 '''const auto alias_ms=ms(alias_start);const auto lifecycle_prefix_ms=ms(lifecycle_start)-oracle_ms;
        pls03::finish(bound,alias_ms);
        const auto consumer_start=clock_type::now();const auto semantic_hash=fingerprint(final,bound);const auto consumer_ms=ms(consumer_start);''')
s=once(s,'std::to_string(fingerprint(final,bound))','std::to_string(semantic_hash)')
s=once(s,'report<<"]\\n}\\n";',
 '''const auto summary_events=bound.semantic_events,summary_writes=static_cast<std::uint64_t>(bound.writes.size()),summary_bindings=static_cast<std::uint64_t>(bound.bindings.size());
        const auto summary_cells=final.values.size(),summary_warnings=final.warnings.size();
        const auto release_start=clock_type::now();bound={};final={};const auto release_ms=ms(release_start);
        report<<"],\\n\\"consumer_ms\\":"<<consumer_ms<<",\\n\\"ir_release_ms\\":"<<release_ms
              <<",\\n\\"lifecycle_ms\\":"<<lifecycle_prefix_ms+consumer_ms+release_ms<<"\\n}\\n";''')
pos=s.index('std::cout<<"parallel_local_source02,')
last=s.index('\n',pos)
line=s[pos:last]
for before,after in [('bound.semantic_events','summary_events'),('bound.writes.size()','summary_writes'),('bound.bindings.size()','summary_bindings'),('final.values.size()','summary_cells'),('final.warnings.size()','summary_warnings')]:line=line.replace(before,after)
s=s[:pos]+line+s[last:]
(out/'main.cpp').write_text(s)
