"""Ownership only: retain PLS06 dense write heads and original semantic rows."""
from pathlib import Path
import subprocess,sys
repo,out=map(lambda p:Path(p).resolve(),sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
subprocess.run([sys.executable,str(repo/'experiments/parallel-local-source-06/generate.py'),str(repo),str(out)],check=True)
def once(s,old,new):
 assert s.count(old)==1,(old,s.count(old));return s.replace(old,new)
s=(out/'aliases.hpp').read_text()
marker='// Symbolic storage linking:'
s=once(s,marker,'} // namespace pls02\n#include "'+(repo/'experiments/parallel-local-source-07/ownership.hpp').as_posix()+'"\nnamespace pls02 {\n'+marker)
s=once(s,'pls03::boundary(pls03::reduction);','pls07::release_endpoints(model);pls03::boundary(pls03::reduction);')
(out/'aliases.hpp').write_text(s)
s=(out/'main.cpp').read_text()
# The consumer takes explicit read-only views; no bound_model API is accepted.
start=s.index('std::uint64_t fingerprint(');end=s.index('\n}\n}',start)+2
f=s[start:end].replace('const bound_model& bound','pls07::consumer_view data').replace('bound.paths','data.paths()').replace('bound.declaration_origins','data.declarations()').replace('bound.writes[v.write]','data.write_at(v.write)')
s=s[:start]+f+s[end:]
s=once(s,'pls04::phase_begin(pls04::aliases);','const pls07::report_counts counts(bound);pls04::phase_begin(pls04::aliases);')
s=once(s,'const auto lifecycle_prefix_ms=ms(lifecycle_start)-oracle_ms;', '''
        pls03::boundary(pls03::off);pls03::running=false;
        const auto ownership_start=clock_type::now();std::optional<pls07::owned_result> owner;
        const std::array<const void*,8> before={bound.paths.data(),bound.writes.data(),bound.bindings.data(),bound.declaration_origins.data(),final.cells.data(),final.values.data(),final.bindings.data(),final.warnings.data()};
        const auto verify_start=clock_type::now();const auto old_history=pls07::verify()?pls07::history_fingerprint(pls07::consumer_view(bound)):0;
        double verification_ms=ms(verify_start);
        if(pls07::enabled()){
            owner.emplace(std::move(final),std::move(bound));pls07::buffers_moved=(before==owner->addresses());
            check(pls07::buffers_moved,"Ownership transfer copied buffers");bound={};
            pls07::bound_empty=bound.bytes()==0&&bound.writes.empty()&&bound.bindings.empty()&&bound.endpoints.empty();check(pls07::bound_empty,"Bound retained buffers");
        }
        const result& final_state=owner?owner->state():final;
        const auto retained=owner?owner->view():pls07::consumer_view(bound);
        pls07::transfer_ms=ms(ownership_start)-verification_ms;
        if(pls07::verify()){const auto t=clock_type::now();pls07::validate(final_state,retained);
            for(const auto& d:retained.declarations()){valid_origin(d.name);valid_origin(d.statement);}
            for(id i=0;i<retained.write_count();++i){const auto w=retained.write_at(i);valid_origin(w.at);valid_origin(w.value_at);}
            for(id i=0;i<retained.binding_count();++i){const auto b=retained.binding_at(i);valid_origin(b.at);valid_origin(b.value_at);}
            pls07::history_hash=pls07::history_fingerprint(retained);check(pls07::history_hash==old_history,"History lost in ownership transfer");pls07::verified=true;verification_ms+=ms(t);}
        const auto retained_bytes=owner?owner->bytes():bound.bytes()+final.cells.capacity()*sizeof(cell)+final.values.capacity()*sizeof(winner)+final.bindings.capacity()*sizeof(final_binding)+final.warnings.capacity()*sizeof(warning);
        const auto lifecycle_prefix_ms=ms(lifecycle_start)-oracle_ms-verification_ms;''')
s=once(s,'fingerprint(final,bound)','fingerprint(final_state,retained)')
# Everything after transfer must consume owned state/views or saved scalar counts.
pos=s.index('const auto consumer_start=');a,b=s[:pos],s[pos:]
for old,new in [('final.cells','final_state.cells'),('final.values','final_state.values'),('final.bindings','final_state.bindings'),('final.warnings','final_state.warnings'),('final.reference_follows','final_state.reference_follows'),('final.default_follows','final_state.default_follows'),('final.link_rounds','final_state.link_rounds'),('bound.declarations','counts.declarations'),('bound.semantic_events','counts.events'),('bound.endpoint_requests','counts.requests'),('bound.declaration_origins.size()','counts.origins'),('bound.writes.size()','counts.writes'),('bound.bindings.size()','counts.bindings'),('bound.bytes()','counts.input_bytes'),('bound.paths','retained.paths()'),('bound.writes[w.write]','retained.write_at(w.write)')]:b=b.replace(old,new)
b=once(b,'bound={};final={};','bound={};final={};owner.reset();')
b=once(b,'pls06::report(std::getenv("PLS06_HEADS_PATH"));','pls06::report(std::getenv("PLS06_HEADS_PATH"));pls07::report(std::getenv("PLS07_OWNER_PATH"),retained_bytes);')
(out/'main.cpp').write_text(a+b)
