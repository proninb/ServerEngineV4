#pragma once
#include "model.hpp"
#include "../../server_engine/project/parser/source_endpoint_resolver.hpp"
#include "../../server_engine/project/parser/enum_constants.hpp"
#include "../../server_engine/project/graph/construction_semantics.hpp"
#include <algorithm>
#include <unordered_map>
#ifdef PLS03_PROBE
#include "../parallel-local-source-03/probe.hpp"
#endif

namespace pls02 {
inline type_ref unqualified(const graph& G,type_ref t){derived_type_record d;
    while(G.derived(t,d)&&(d.kind==derived_type_kind::const_qualified||d.kind==derived_type_kind::volatile_qualified))t=d.child;return t;}
inline bool reference(const graph& G,type_ref t,type_ref* child=nullptr){derived_type_record d;
    if(!G.derived(unqualified(G,t),d)||(d.kind!=derived_type_kind::lvalue_reference&&d.kind!=derived_type_kind::rvalue_reference))return false;
    if(child)*child=d.child;return true;}
inline bool scalar_target(const graph& G,type_ref t){derived_type_record d;
    while(G.derived(t,d)){if(d.kind==derived_type_kind::const_qualified)return false;if(d.kind!=derived_type_kind::volatile_qualified)break;t=d.child;}
    if(t.kind()==type_ref_kind::intrinsic)return t.payload()!=static_cast<unsigned>(intrinsic_type::void_type);
    type_handle h;if(G.named(t,h)){const auto* e=G.find(h);return e&&e->kind==graph_type_kind::enumeration;}
    return G.derived(t,d)&&d.kind==derived_type_kind::pointer;
}
struct typed_endpoint {identity_ref who{};type_ref root_type{},type{};range steps{};origin at{};};
struct write {id target=none;construction_value value{};order event{};origin at{},value_at{};bool declaration=false;};
struct binding {id target=none,source=none;order event{};origin at{},value_at{};};
struct declaration_origin {identity_ref who{};origin name{},statement{};order event{};bool is_extern=false;};
struct bound_model {
    std::vector<endpoint_path_step> paths;
    std::vector<typed_endpoint> endpoints;
    std::vector<write> writes;
    std::vector<binding> bindings;
    std::vector<declaration_origin> declaration_origins;
    std::uint64_t declarations=0,semantic_events=0,endpoint_requests=0;
    std::size_t bytes() const {return paths.capacity()*sizeof(endpoint_path_step)+endpoints.capacity()*sizeof(typed_endpoint)+writes.capacity()*sizeof(write)+bindings.capacity()*sizeof(binding)+declaration_origins.capacity()*sizeof(declaration_origin);}
};

// No lexical_generation/file_context/Source buffer is accepted by this API.
class semantic_linker {
    graph& G;string_table& strings;identity_space& identities;source_endpoint_resolver resolver;
    bound_model out;
    struct extern_state {bool defined=false;origin at{};order event{};};
    std::unordered_map<id,extern_state> externs;
    const model* local=nullptr;
    std::vector<string_id> name_ids;
    std::vector<identity_ref> scopes;
    std::vector<type_ref> types;
    order current{};
    [[noreturn]] void fail(const char* text,source_range at) const {throw failure(text,{local->input.file,at},current);}
    void status(server_status s,source_range at,const char* message){if(!succeeded(s))fail(message,at);}
    string_id name(id n){auto& r=name_ids.at(n);if(!r)status(strings.intern(local->name(n),r),{},"String interning failed");return r;}
    identity_ref scope_id(id n){if(n==0)return identities.root();auto& result=scopes.at(n);if(result)return result;
        const auto& s=local->scopes[n];const auto parent=scope_id(s.parent);
        status(identities.resolve(parent,name(s.name),identity_kind::namespace_scope,result),s.at,"Namespace resolution failed");return result;}
    identity_ref visible(identity_ref scope,string_id n,identity_kind kind) const {
        for(auto p=scope;p;){if(auto found=identities.find(p,n,kind))return found;
            if(p==identities.root())break;identity_record record;if(!identities.record(p,record))break;p=record.parent;}return {};
    }
    identity_ref symbol_entity(id symbol_id,identity_kind kind){const auto& s=local->symbols.at(symbol_id);const auto c=std::span<const name_use>{local->components}.subspan(s.components.begin,s.components.count);
        auto base=scope_id(s.scope);if(c.size()==1)return visible(base,name(c[0].name),kind);
        auto ns=visible(base,name(c[0].name),identity_kind::namespace_scope);
        auto ty=visible(base,name(c[0].name),identity_kind::type);
        for(std::size_t i=1;i<c.size();++i){const auto parent=ns?ns:ty;if(!parent)return {};
            if(i+1==c.size())return identities.find(parent,name(c[i].name),kind);
            ns=identities.find(parent,name(c[i].name),identity_kind::namespace_scope);ty=identities.find(parent,name(c[i].name),identity_kind::type);}
        return {};
    }
    type_ref type(id n){auto& result=types.at(n);if(result)return result;const auto& t=local->types[n];
        if(t.builtin!=intrinsic_type::none)result=G.intrinsic(t.builtin);
        else {const auto who=symbol_entity(t.named,identity_kind::type);const auto* e=G.find(G.find_type(who));if(!e)fail("Source type absent in Header catalog",t.at);
            result=e->kind==graph_type_kind::intrinsic_alias?G.intrinsic(e->alias_intrinsic()):G.named(who);}
        const auto wrap=[&](derived_type_kind k,std::uint64_t payload){type_ref next;status(G.derive_resolved(result,k,payload,next),t.at,"Derived type failed");result=next;};
        if(t.qualifiers&1)wrap(derived_type_kind::const_qualified,0);if(t.qualifiers&2)wrap(derived_type_kind::volatile_qualified,0);
        if(t.reference)wrap(t.reference==1?derived_type_kind::lvalue_reference:derived_type_kind::rvalue_reference,0);
        for(id i=t.extents.count;i;--i)wrap(derived_type_kind::bounded_array,local->extents[t.extents.begin+i-1]);
        return result;
    }
    id save(identity_ref who,type_ref root,type_ref final,std::span<const endpoint_path_step> path,source_range at){
        const auto begin=static_cast<id>(out.paths.size());out.paths.insert(out.paths.end(),path.begin(),path.end());
        out.endpoints.push_back({who,root,final,{begin,static_cast<id>(path.size())},{local->input.file,at}});return static_cast<id>(out.endpoints.size()-1);
    }
    id endpoint_request(id n){const auto& request=local->endpoints.at(n);const auto who=symbol_entity(request.symbol,identity_kind::object);
        source_typed_endpoint value;source_endpoint_resolver::failure error;
        if(!succeeded(resolver.begin_endpoint(who,value,error)))fail("Object must be declared before use (or extern)",request.at);
        for(const auto& s:std::span<const selector>{local->selectors}.subspan(request.selectors.begin,request.selectors.count)){
            const auto location=s.source();
            server_status status_;
            if(s.kind()==selector_kind::member){type_handle record,owner;status_=resolver.prepare_member(value,record,error);
                if(succeeded(status_))status_=resolver.append_member(value,record,name(static_cast<id>(s.value)),owner,error);
            }else{derived_type_record array;if(!resolver.array_type(value.type,array))fail("Index requires bounded array",location);
                status_=resolver.append_index(value,array,s.value,error);}
            if(!succeeded(status_))fail("Invalid member/index transition",location);
        }
        ++out.endpoint_requests;return save(who,value.object_type,value.type,value.path,request.at);
    }
    construction_value scalar(const value& v){
        if(v.kind==value_kind::number)return v.number;
        if(v.kind!=value_kind::symbol)fail("Expected scalar constant",v.at);
        const auto& e=local->endpoints[v.endpoint];if(e.selectors.count)fail("Value-to-value copies unsupported",v.at);
        const auto& s=local->symbols[e.symbol];const auto cc=std::span<const name_use>{local->components}.subspan(s.components.begin,s.components.count);
        auto scope=scope_id(s.scope);construction_value result;
        if(visible(scope,name(cc[0].name),identity_kind::object))fail("Source object is not a scalar constant",v.at);
        if(cc.size()==1){
            if(!enum_constant(G,identities,scope,name(cc[0].name),result))fail("Unknown enum constant",v.at);return result;}
        auto ns=visible(scope,name(cc[0].name),identity_kind::namespace_scope),owner=visible(scope,name(cc[0].name),identity_kind::type);
        for(std::size_t i=1;i<cc.size();++i){const auto parent=ns?ns:owner;if(!parent)fail("Unknown qualified constant",v.at);
            if(i+1==cc.size() && enum_constant(G,identities,parent,name(cc[i].name),result,true))return result;
            ns=identities.find(parent,name(cc[i].name),identity_kind::namespace_scope);owner=identities.find(parent,name(cc[i].name),identity_kind::type);}
        fail("Unknown qualified enum constant",v.at);
    }
    void append_write(id ep,construction_value v,source_range value_at,bool declaration=false){
        const auto& endpoint=out.endpoints[ep];if(!declaration&&!scalar_target(G,endpoint.type))fail("Invalid scalar/const initialization target",endpoint.at.source);
        if(!normalize_enum_constant(G,endpoint.type,v)||!construction_compatible(G,endpoint.type,v))fail("Initializer type mismatch",value_at);
        out.writes.push_back({ep,v,current,endpoint.at,{local->input.file,value_at},declaration});
    }
    void assign(id ep,id value_id){const auto& v=local->values.at(value_id);const auto target=out.endpoints[ep];type_ref child;
        if(reference(G,target.type,&child)){
            if(v.kind!=value_kind::symbol)fail("Reference binding needs endpoint",v.at);
            const auto source=endpoint_request(v.endpoint);auto st=out.endpoints[source].type;type_ref inner;if(reference(G,st,&inner))st=inner;
            if(child!=st)fail("Reference link types differ",v.at);
            out.bindings.push_back({ep,source,current,target.at,{local->input.file,v.at}});return;
        }
        if(v.kind==value_kind::string){derived_type_record array;intrinsic_type ch;
            if(!G.derived(unqualified(G,target.type),array)||array.kind!=derived_type_kind::bounded_array||!G.intrinsic(array.child,ch)||ch!=intrinsic_type::char_type)
                fail("String requires char array",v.at);
            if(v.bytes.count+1>array.payload)fail("String exceeds array bound",v.at);
            // Copy the typed prefix once; no Source endpoint syntax is read.
            std::vector<endpoint_path_step> path(out.paths.begin()+target.steps.begin,out.paths.begin()+target.steps.begin+target.steps.count);
            path.push_back({0,endpoint_path_step_kind::array_index,{}});
            for(std::uint64_t i=0;i<array.payload;++i){path.back().value=i;
                const auto element=save(target.who,target.root_type,array.child,path,target.at.source);
                const auto byte=i<v.bytes.count?local->bytes[v.bytes.begin+static_cast<id>(i)]:0;
                append_write(element,construction_value::constant(construction_kind::unsigned_integer,byte),v.at);}
            return;
        }
        append_write(ep,scalar(v),v.at);
    }
    void declare(const event& e){const auto& symbol=local->symbols[e.symbol];const auto use=local->components[symbol.components.begin];const auto t=type(e.type);
        construction_value initial{};std::uint32_t flags=0;
        if(e.value!=none){initial=scalar(local->values[e.value]);if(!normalize_enum_constant(G,t,initial)||!construction_compatible(G,t,initial))fail("Declaration initializer mismatch",e.at);flags=graph_object_non_default_initializer;}
        identity_ref who;status(identities.resolve(scope_id(e.scope),name(use.name),identity_kind::object,who),e.at,"Object WHO failed");
        out.declaration_origins.push_back({who,{local->input.file,e.at},{local->input.file,local->types[e.type].at},current,bool(e.flags&1)});
        const auto existing=G.find_object(who);auto found=externs.find(who.value());object_handle object;
        if(e.flags&1){if(existing){if(G.find(existing)->type!=t)fail("Extern type conflict",e.at);}else status(G.add_resolved_object(who,t,object),e.at,"Extern registration failed");
            if(found==externs.end())externs[who.value()]={bool(existing),{local->input.file,e.at},current};
        }else{if(existing){if(found==externs.end()||found->second.defined)fail("Duplicate object definition",e.at);
                status(G.complete_resolved_extern_object(who,t,object,flags,initial),e.at,"Extern definition type conflict");found->second.defined=true;
            }else status(G.add_resolved_object(who,t,object,flags,initial),e.at,"Object registration failed");
            ++out.declarations;
            if(e.value!=none){const auto ep=save(who,t,t,{},e.at);append_write(ep,initial,local->values[e.value].at,true);}
        }
    }
public:
    semantic_linker(graph& g,string_table& s,identity_space& ids):G(g),strings(s),identities(ids),resolver(g){}
    bound_model run(const std::vector<model>& inputs){
#ifdef PLS03_PROBE
        pls03::lifetime_begin(inputs);
#endif
        std::vector<const model*> ordered;for(const auto& m:inputs)ordered.push_back(&m);
        std::sort(ordered.begin(),ordered.end(),[](auto a,auto b){return a->input.source_order<b->input.source_order;});
        for(id i=0;i<ordered.size();++i){local=ordered[i];if(local->input.source_order!=i)throw failure("Invalid Source task ranks");
            name_ids.assign(local->names.size(),{});scopes.assign(local->scopes.size(),{});types.assign(local->types.size(),{});
            for(const auto& e:local->events){current={i,e.local_order};++out.semantic_events;
                if(e.kind==event_kind::namespace_scope)(void)scope_id(e.scope);
                else if(e.kind==event_kind::declare)declare(e);
                else assign(endpoint_request(e.target),e.value);
            }
            if(!local->error.empty())throw failure(local->error,{local->input.file,local->error_at},{i,static_cast<id>(local->events.size())});
#ifdef PLS03_PROBE
            pls03::lifetime_file(*local,out.bytes());
#endif
        }
        const extern_state* earliest=nullptr;
        for(const auto& e:externs)if(!e.second.defined && (!earliest || e.second.event<earliest->event))earliest=&e.second;
        if(earliest)throw failure("Source extern has no definition",earliest->at,earliest->event);
        return std::move(out);
    }
};
}
