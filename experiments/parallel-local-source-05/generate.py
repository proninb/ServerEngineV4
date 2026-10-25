"""PLS04 baseline unchanged; separate allocation-tagged ownership audit."""
from pathlib import Path
import subprocess,sys,re
repo,out=map(lambda p:Path(p).resolve(),sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
control=out/'control';audit=out/'audit'
for target in (control,audit):
 subprocess.run([sys.executable,str(repo/'experiments/parallel-local-source-04/generate.py'),str(repo),str(target)],check=True)
def once(s,old,new):
 assert s.count(old)==1,(old,s.count(old));return s.replace(old,new)
header=(repo/'experiments/parallel-local-source-05/audit.hpp').as_posix()
s=(repo/'experiments/parallel-local-source-04/memory.hpp').read_text()
s=once(s,'#ifdef PLS04_ALLOCATIONS','#include "'+header+'"\n#ifdef PLS04_ALLOCATIONS')
s=once(s,'phase owner;','phase owner;pls05::tag tag;')
s=once(s,'h->owner=active;','h->owner=active;h->tag=pls05::active;')
s=once(s,'if(active!=off){auto& s=heap[active];','if(active!=off){pls05::allocated(h->tag,requested,active==aliases);auto& s=heap[active];')
s=once(s,'if(h->owner!=off){auto& s=heap[h->owner];','if(h->owner!=off){pls05::freed(h->tag,h->bytes);auto& s=heap[h->owner];')
(audit/'memory.hpp').write_text(s)
s=(audit/'bind.hpp').read_text().replace((repo/'experiments/parallel-local-source-04/memory.hpp').as_posix(),(audit/'memory.hpp').as_posix())
for typ,name,tag in [('endpoint_path_step','paths','bound_paths'),('typed_endpoint','endpoints','bound_endpoints'),('write','writes','bound_writes'),('binding','bindings','bound_bindings'),('declaration_origin','declaration_origins','provenance')]:
 s=once(s,f'std::vector<{typ}> {name};',f'pls05::vector<{typ}> {name}=pls05::vector<{typ}>(pls05::{tag});')
s=once(s,'struct bound_model {','struct bound_model {\n using path_value_type=endpoint_path_step;using endpoint_value_type=typed_endpoint;using write_value_type=write;using binding_value_type=binding;using origin_value_type=declaration_origin;')
(audit/'bind.hpp').write_text(s)
s=(repo/'experiments/parallel-local-source-03/flat.hpp').read_text()
s=s.replace('#include "probe.hpp"','#include "'+(repo/'experiments/parallel-local-source-03/probe.hpp').as_posix()+'"')
s=once(s,'std::vector<flat_slot> slots;','pls05::vector<flat_slot> slots=pls05::vector<flat_slot>(pls05::flat_index);')
s=once(s,'std::vector<flat_slot> next(slots.empty()?8:slots.size()*2);','pls05::vector<flat_slot> next(pls05::flat_index,slots.empty()?8:slots.size()*2);')
(audit/'flat.hpp').write_text(s)
s=(audit/'aliases.hpp').read_text().replace((repo/'experiments/parallel-local-source-03/flat.hpp').as_posix(),(audit/'flat.hpp').as_posix())
assert s.count('const std::vector<endpoint_path_step>* paths;')==2
s=s.replace('const std::vector<endpoint_path_step>* paths;','const decltype(bound_model::paths)* paths;')
for typ,name,tag in [('cell','cells','canonical_cells'),('winner','values','final_values'),('final_binding','bindings','final_bindings'),('warning','warnings','final_warnings')]:
 s=once(s,f'std::vector<{typ}> {name};',f'pls05::vector<{typ}> {name}=pls05::vector<{typ}>(pls05::{tag});')
s=once(s,'std::unordered_map<id,id> bindings,cache,header_bindings;',
 'pls05::map<id,id> bindings{pls05::binding_lookup},cache{pls05::reference_cache},header_bindings{pls05::header_lookup};')
s=once(s,'std::unordered_set<id> visiting;','pls05::set<id> visiting{pls05::cycle_set};')
s=once(s,'std::unordered_map<id,id>& into','pls05::map<id,id>& into')
s=once(s,'std::unordered_map<id,id> direct;std::vector<id> indirect;',
 'pls05::map<id,id> direct{pls05::direct_and_convergence};pls05::vector<id> indirect(pls05::direct_and_convergence);')
old='std::unordered_map<id,id> heads;std::vector<id> next(model.'
for field,head,link in [('bindings','binding_heads','binding_next'),('writes','write_heads','write_next')]:
 s=once(s,old+field+'.size(),none);',f'pls05::map<id,id> heads{{pls05::{head}}};pls05::vector<id> next(pls05::{link},model.{field}.size(),none);')
for name,tag in [('keys','binding_keys'),('hh','binding_history'),('history','write_history')]:
 s=once(s,f'std::vector<id> {name};',f'pls05::vector<id> {name}(pls05::{tag});')
for signature in ['id canonical(const typed_endpoint& e,bool follow_final,unsigned depth=0){','typed_endpoint default_source(id c){','id import_endpoint(object_endpoint e){']:
 s=once(s,signature,signature+'pls05::scope temporary_scope(pls05::traversal_temporaries);')
# Audit checkpoint positions have original local scratch still alive unless named
# cleanup/after-release. No algorithm/lifetime is changed by observation.
for phase,label in [('header','alias_start'),('catalog','header_imported'),('references','binding_catalog_done'),('binding_targets','reference_resolution_done'),('binding_diagnostics','binding_targets_done'),('binding_cleanup','binding_diagnostics_done'),('write_targets','binding_scratch_released'),('reduction','write_targets_done'),('value_order','lww_done'),('write_cleanup','value_order_done'),('diagnostic_order','write_scratch_released')]:
 needle=f'pls03::boundary(pls03::{phase});'
 s=once(s,needle,f'pls05::snapshot("{label}",model,out);'+needle)
s=once(s,'pls03::flat_capacity_bytes=flat.bytes();','pls05::snapshot("output_ready_scratch_alive",model,out);pls05::analyze(model,out);pls03::flat_capacity_bytes=flat.bytes();')
(audit/'aliases.hpp').write_text(s)
s=(audit/'main.cpp').read_text()
s=once(s,'const auto alias_ms=ms(alias_start);','const auto alias_ms=ms(alias_start);pls05::snapshot("alias_scratch_released",bound,final);')
s=once(s,'pls04::finish();pls04::write','pls04::finish();pls05::write(std::getenv("PLS05_AUDIT_PATH"));pls04::write')
(audit/'main.cpp').write_text(s)
