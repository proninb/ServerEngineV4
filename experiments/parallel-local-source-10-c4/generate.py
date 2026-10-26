"""Project-visible catalog followed by ordered temporal commit, one syntax pass."""
from pathlib import Path
import subprocess,sys
repo,out=map(lambda x:Path(x).resolve(),sys.argv[1:]);own=repo/'experiments/parallel-local-source-10-c4';parent=repo/'experiments/parallel-local-source-10-c3'
subprocess.run([sys.executable,str(parent/'generate.py'),str(repo),str(out)],check=True)
def once(s,a,b):
 assert s.count(a)==1,(a,s.count(a));return s.replace(a,b)
s=(out/'ordered_binder.hpp').read_text()
s=once(s,'std::unordered_set<id> declared;','std::optional<failure> registration_error;')
s=once(s,'struct extern_state {bool defined=false;origin at{};order event{};};','struct extern_state {bool defined=false;origin at{};order event{};bool invalid=false;};')
s=once(s,'if(!declared.contains(who.value()))fail("Object needs a declaration or extern in this Source file",request.at);','')
s=s.replace('Object must be declared before use (or extern)','Unknown object in project catalog')
a=s.index('    void declare(const event& e)');b=s.index('public:\n    ordered_committer',a)
s=s[:a]+'''    void register_object(const event& e){
        const auto& symbol=local->symbols[e.symbol];const auto use=local->components[symbol.components.begin];const auto t=type(e.type);
        identity_ref who;status(identities.resolve(scope_id(e.scope),name(use.name),identity_kind::object,who),e.at,"Object WHO failed");
        const auto existing=G.find_object(who);
        auto [entry,inserted]=externs.try_emplace(who.value(),extern_state{bool(existing),{local->input.file,e.at},current});
        if(existing&&G.find(existing)->type!=t){entry->second.invalid=true;fail("Project object type conflict",e.at);}
        if(!(e.flags&1)&&entry->second.defined)fail("Duplicate object definition",e.at);
        if(!existing){object_handle object;status(G.add_resolved_object(who,t,object),e.at,"Object registration failed");}
        if(!(e.flags&1))entry->second.defined=true;
    }
    void declare(const event& e){
        const auto t=type(e.type);const auto who=symbol_entity(e.symbol,identity_kind::object);
        if(!(e.flags&1)){
            construction_value initial{};std::uint32_t flags=0;
            if(e.value!=none){initial=scalar(local->values[e.value]);if(!normalize_enum_constant(G,t,initial)||!construction_compatible(G,t,initial))fail("Declaration initializer mismatch",e.at);flags=graph_object_non_default_initializer;}
            object_handle object;status(G.complete_resolved_extern_object(who,t,object,flags,initial),e.at,"Object definition completion failed");
            ++out.declarations;
            if(e.value!=none){const auto ep=save(who,t,t,{},e.at);append_write(ep,initial,local->values[e.value].at,true);}
        }
    }
    void attach(const model& input){local=&input;name_ids.assign(local->names.size(),{});scopes.assign(local->scopes.size(),{});types.assign(local->types.size(),{});}
''' +s[b:]
s=once(s,'    void apply(const model& input){\n        local=&input;declared.clear();name_ids.assign(local->names.size(),{});scopes.assign(local->scopes.size(),{});types.assign(local->types.size(),{});','''    void register_file(const model& input){
        attach(input);
        for(const auto& e:local->events){current={local->input.source_order,e.local_order};
            try{if(e.kind==event_kind::namespace_scope)(void)scope_id(e.scope);else if(e.kind==event_kind::declare)register_object(e);}
            catch(const failure& error){if(!registration_error)registration_error=error;}
        }
    }
    void seal_catalog(){
        for(const auto& [who,state]:externs)if(!state.defined&&!state.invalid&&(!registration_error||state.event<registration_error->event))
            registration_error=failure("Source extern has no definition",state.at,state.event);
    }
    void apply(const model& input){
        attach(input);''')
s=once(s,'current={local->input.source_order,e.local_order};++out.semantic_events;', 'current={local->input.source_order,e.local_order};if(registration_error&&!(current<registration_error->event))throw *registration_error;++out.semantic_events;')
s=once(s,'declared.insert(who.value());','')
a=s.index('    void finish(){');s=s[:a]+'''    void finish(){if(registration_error)throw *registration_error;}
};
}
'''
s='#include <optional>\n'+s;(out/'ordered_binder.hpp').write_text(s)
p=(parent/'publish_source.hpp').read_text()
p=once(p,'#include "stats.hpp"','#include "'+(own/'stats.hpp').as_posix()+'"')
p=once(p,'const auto n=tasks.size();','const auto n=tasks.size();std::vector<pls02::model> models(n);pls10c4::local_capacity=0;pls10c4::registration_ms=0;pls10c4::commit_ms=0;')
p=once(p,'''   if(!succeeded(sources.begin_root(tasks[rank].file)))throw pls02::failure("SourceMap begin failed");
   commit.apply(*value);if(!succeeded(sources.end_root()))throw pls02::failure("SourceMap end failed");value.reset();''','''   models[rank]=std::move(*value);value.reset();pls10c4::local_capacity+=models[rank].retained_bytes();
   const auto registration_start=clock::now();commit.register_file(models[rank]);pls10c4::registration_ms+=elapsed(registration_start);''')
p=once(p,'front=rank+1;++pls10c3::files_committed;','front=rank+1;')
p=once(p,'  commit.finish();linking_ms=elapsed(at);alias_ms=writer_ms=extraction_ms=release_ms=0;','  pls10c4::catalog_wall_ms=elapsed(at);commit.seal_catalog();')
p=once(p,'pool.clear();lexical.release_source_tokens();','''pool.clear();lexical.release_source_tokens();
  at=clock::now();
  for(std::size_t rank=0;rank<n;++rank){
   if(!succeeded(sources.begin_root(tasks[rank].file)))throw pls02::failure("SourceMap begin failed");
   commit.apply(models[rank]);if(!succeeded(sources.end_root()))throw pls02::failure("SourceMap end failed");
   models[rank]={};++pls10c3::files_committed;
  }
  commit.finish();pls10c4::commit_ms=elapsed(at);linking_ms=pls10c4::catalog_wall_ms+pls10c4::commit_ms;alias_ms=writer_ms=extraction_ms=release_ms=0;''')
(out/'project_source.hpp').write_text(p)
p=(out/'publish_parser.cpp').read_text().replace((parent/'publish_source.hpp').as_posix(),(out/'project_source.hpp').as_posix());(out/'publish_parser.cpp').write_text(p)
b=(out/'publish_main.cpp').read_text().replace((parent/'stats.hpp').as_posix(),(own/'stats.hpp').as_posix()).replace('PLS10C3.','PLS10C4.')
b=once(b,'pls10c3::print(std::cout);','pls10c4::print(std::cout);pls10c3::print(std::cout);');(out/'publish_main.cpp').write_text(b)
