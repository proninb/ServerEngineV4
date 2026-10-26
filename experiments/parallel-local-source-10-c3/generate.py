from pathlib import Path
import subprocess,sys
repo,out=map(lambda x:Path(x).resolve(),sys.argv[1:]);own=repo/'experiments/parallel-local-source-10-c3'
subprocess.run([sys.executable,str(repo/'experiments/parallel-local-source-08/generate.py'),str(repo),str(out)],check=True)
def once(s,a,b):
 assert s.count(a)==1,(a,s.count(a));return s.replace(a,b)
s=(repo/'experiments/parallel-local-source-02/bind.hpp').read_text()
s=s[s.index('// No lexical_generation'):s.rfind('\n}')]
s=once(s,'class semantic_linker {','class ordered_committer {')
s=once(s,'bound_model out;', '''struct scratch {std::vector<endpoint_path_step> paths;std::vector<typed_endpoint> endpoints;
        std::uint64_t declarations=0,semantic_events=0,endpoint_requests=0;} out;
    pls10c3::commit sink;
    std::unordered_set<id> declared;''')
s=once(s,'source_typed_endpoint value;source_endpoint_resolver::failure error;', '''if(!declared.contains(who.value()))fail("Object needs a declaration or extern in this Source file",request.at);
        source_typed_endpoint value;source_endpoint_resolver::failure error;''')
s=once(s,'out.writes.push_back({ep,v,current,endpoint.at,{local->input.file,value_at},declaration});','sink.write(endpoint,out.paths,v,declaration);')
s=once(s,'out.bindings.push_back({ep,source,current,target.at,{local->input.file,v.at}});return;', 'sink.bind(target,out.endpoints[source],out.paths);return;')
s=once(s,'out.declaration_origins.push_back({who,{local->input.file,e.at},{local->input.file,local->types[e.type].at},current,bool(e.flags&1)});','')
start=s.index('public:\n    semantic_linker')
s=s[:start]+'''public:
    ordered_committer(graph& g,string_table& s,identity_space& ids,source_map& sources,std::vector<parser_warning>* warnings)
      :G(g),strings(s),identities(ids),resolver(g),sink(g,sources,warnings){}
    void apply(const model& input){
        local=&input;declared.clear();name_ids.assign(local->names.size(),{});scopes.assign(local->scopes.size(),{});types.assign(local->types.size(),{});
        for(const auto& e:local->events){current={local->input.source_order,e.local_order};++out.semantic_events;
            if(e.kind==event_kind::namespace_scope)(void)scope_id(e.scope);
            else if(e.kind==event_kind::declare){declare(e);const auto who=symbol_entity(e.symbol,identity_kind::object);declared.insert(who.value());sink.at({local->input.file,e.at});sink.declaration(who);}
            else assign(endpoint_request(e.target),e.value);
            out.endpoints.clear();out.paths.clear();
        }
        if(!local->error.empty())throw failure(local->error,{local->input.file,local->error_at});
    }
    void finish(){const extern_state* earliest=nullptr;for(const auto& e:externs)if(!e.second.defined&&(!earliest||e.second.event<earliest->event))earliest=&e.second;
        if(earliest)throw failure("Source extern has no definition",earliest->at,earliest->event);}
};
'''
(out/'ordered_binder.hpp').write_text('#pragma once\n#include "'+(own/'commit.hpp').as_posix()+'"\n#include <unordered_set>\nnamespace pls02 {\n'+s+'}\n')
# Generate the same native PUBLISH orchestration; only the experiment hook changes.
p=(out/'publish_parser.cpp').read_text().replace((repo/'experiments/parallel-local-source-08/publish_source.hpp').as_posix(),(own/'publish_source.hpp').as_posix())
(out/'publish_parser.cpp').write_text(p)
b=(out/'publish_main.cpp').read_text();b=once(b,'pls08c::print(std::cout);','pls08c::print(std::cout);pls10c3::print(std::cout);')
b='#include "'+(own/'stats.hpp').as_posix()+'"\n'+b
b=b.replace('PLS08C.', 'PLS10C3.')
(out/'publish_main.cpp').write_text(b)
