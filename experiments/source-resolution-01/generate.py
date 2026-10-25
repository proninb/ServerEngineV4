"""Generate an isolated instrumented Source translation unit; Graph stays original."""
from pathlib import Path
import hashlib,json,re,sys
repo,out=map(lambda p:Path(p).resolve(),sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
p=repo/'server_engine/project/parser/source_parser_v2.cpp'
s=p.read_text(encoding='utf-8-sig')
s=re.sub(r'#include "([^"]+)"',lambda m:'#include "'+(p.parent/m[1]).resolve().as_posix()+'"',s)
s='#include "'+(repo/'experiments/source-resolution-01/capture.hpp').as_posix()+'"\n'+s
# Replace only explicit Source API spelling; diagnostics are not a C++ interpreter.
s=s.replace('input.next(current)','sres01::next(input,current,strings)').replace('input.next(buffered)','sres01::next(input,buffered,strings)')
assert not re.search(r'\binput\.next\(',s)
for api,new in [('begin_root','prov_begin'),('end_root','prov_end'),('add_dependency','prov_dependency'),('add_initialization','prov_init'),('add','prov_add')]:
 pattern=r'\bsources\.'+api+r'\s*\('
 s,n=re.subn(pattern,'sres01::'+new+'(sources'+('' if api=='end_root' else ','),s)
 assert n,(api,n)
 if api=='end_root':s=s.replace('sres01::prov_end(sources)', 'sres01::prov_end(sources)')
s=s.replace('identities.resolve(', 'sres01::resolve(identities,')
needle='''    [[nodiscard]] identity_ref find_visible(identity_ref scope, string_id name,
        identity_kind kind) const noexcept {'''
assert needle in s
s=s.replace(needle,'''    [[nodiscard]] identity_ref find_visible(identity_ref scope,string_id name,identity_kind kind) const noexcept {
        return sres01::find(scope,name,kind,[&]{return find_visible_original(scope,name,kind);});
    }
'''+needle.replace('find_visible(', 'find_visible_original('))
needle='''    [[nodiscard]] server_status read_endpoint(identity_ref scope_id,
        endpoint& output, identity_ref prebound_object = {}) noexcept {'''
assert needle in s
wrapper='''    [[nodiscard]] server_status read_endpoint(identity_ref scope_id,
        endpoint& output,identity_ref prebound_object={}) noexcept {
        auto& state=sres01::state;
        if(state.mode!=1 && state.mode!=5) return read_endpoint_original(scope_id,output,prebound_object);
        if(state.mode==5) {
            if(state.endpoint_at>=state.endpoints.size()) return sres01::bad("endpoint trace exhausted");
            const auto& r=state.endpoints[state.endpoint_at++];
            if(state.verify && (r.scope!=scope_id || r.prebound!=prebound_object || r.has_before!=has_buffered ||
                !sres01::same_token(r.before,current) ||
                (has_buffered && !sres01::same_token(r.buffer_before,buffered))))
                return sres01::bad("endpoint input/cursor mismatch");
            for(std::uint64_t i=0;i<r.token_reads;++i) {
                semantic_token ignored;
                const auto status=sres01::next(input,ignored,strings);
                if(!succeeded(status)) return status;
            }
            if(r.prov_begin>state.provenance.size() || r.prov_count>state.provenance.size()-r.prov_begin)
                return sres01::bad("invalid endpoint dependency range");
            for(std::size_t i=0;i<r.prov_count;++i) {
                const auto& dep=state.provenance[r.prov_begin+i];
                if(dep.kind!=sres01::prov_kind::dependency || sres01::apply_prov(sources,dep)!=dep.status)
                    return sres01::bad("endpoint dependency changed");
            }
            if(r.path_begin>state.steps.size() || r.path_count>state.steps.size()-r.path_begin)
                return sres01::bad("invalid endpoint shape");
            output.reset();output.type=r.type;output.object_type=r.object_type;
            output.ref=r.ref;output.unit={r.unit_file,r.unit_who};output.location=r.location;
            try {
                output.path.reserve(static_cast<std::size_t>(r.path_capacity));
                const auto path=std::span<const endpoint_path_step>{state.steps}.subspan(r.path_begin,r.path_count);
                output.path.assign(path.begin(),path.end());
            } catch(...) {return sres01::bad("endpoint materialization failed");}
            current=r.after;buffered=r.buffer_after;has_buffered=r.has_after;
            return r.status;
        }
        sres01::endpoint_record r;
        r.scope=scope_id;r.prebound=prebound_object;r.before=current;r.buffer_before=buffered;r.has_before=has_buffered;
        const auto reads=state.tokens.size(),prov=state.provenance.size();
        const auto status=read_endpoint_original(scope_id,output,prebound_object);
        r.after=current;r.buffer_after=buffered;r.has_after=has_buffered;
        r.type=output.type;r.object_type=output.object_type;r.ref=output.ref;
        r.unit_file=output.unit.file;r.unit_who=output.unit.who;r.location=output.location;
        r.path_begin=state.steps.size();r.path_count=output.path.size();r.path_capacity=output.path.capacity();
        r.token_reads=state.tokens.size()-reads;r.prov_begin=prov;r.prov_count=state.provenance.size()-prov;r.status=status;
        try {state.steps.insert(state.steps.end(),output.path.begin(),output.path.end());state.endpoints.push_back(r);}
        catch(...) {sres01::bad("endpoint capture failed");}
        return status;
    }
'''
s=s.replace(needle,wrapper+needle.replace('read_endpoint(', 'read_endpoint_original('))
start=s.index('server_status parse_source_roots_v2(');brace=s.index('{',start)
s=s[:brace+1]+'''\n    if(!sres01::begin(frontend_root_count,files,strings,identities,G)) return server_status::project_artifact_invalid;
'''+s[brace+1:]
needle='''    source_parser_v2 parser{files, lexical, configuration, strings,'''
assert s.count(needle)==1
s=s.replace(needle,'''    sres01::start();
    {
'''+needle)
end=s.index('\n} // namespace cw::server',start)
pos=s.rindex('    return server_status::success;',start,end)
s=s[:pos]+'''    }
    if(!sres01::finish(sources)) return server_status::project_artifact_invalid;
'''+s[pos:]
(out/'source_parser_v2.cpp').write_text(s,newline='\r\n')
(out/'manifest.json').write_text(json.dumps({'source':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()},indent=2))
