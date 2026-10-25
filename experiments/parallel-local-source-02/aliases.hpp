#pragma once
#include "bind.hpp"
#include <numeric>
#include <optional>
#include <unordered_set>

namespace pls02 {
struct cell {identity_ref who{};type_ref type{};range steps{};};
struct cell_key {identity_ref who{};range steps{};};
struct cell_hash {
    const std::vector<endpoint_path_step>* paths;
    std::size_t operator()(const cell_key& k) const {
        std::uint64_t h=k.who.value();for(const auto& s:std::span<const endpoint_path_step>{*paths}.subspan(k.steps.begin,k.steps.count)){
            h=(h^static_cast<std::uint64_t>(s.kind))*1099511628211ULL;h=(h^s.value)*1099511628211ULL;}return static_cast<std::size_t>(h);
    }
};
struct cell_equal {
    const std::vector<endpoint_path_step>* paths;
    bool operator()(const cell_key& a,const cell_key& b) const {
        return a.who==b.who && a.steps.count==b.steps.count && std::equal(paths->begin()+a.steps.begin,paths->begin()+a.steps.begin+a.steps.count,paths->begin()+b.steps.begin);
    }
};
struct warning {origin previous{},current{};order previous_event{},current_event{};id cell=none;bool rebind=false;};
struct winner {id cell=none,write=none;};
struct final_binding {id target=none,source=none,binding=none;};
struct result {
    std::vector<cell> cells;
    std::vector<winner> values;
    std::vector<final_binding> bindings;
    std::vector<warning> warnings;
    std::uint64_t reference_follows=0,default_follows=0,link_rounds=0;
};

// Symbolic storage linking: object WHO + a dereference-free typed storage path.
// No source tokens, text, ABI offsets, or resident SHM are consulted.
class alias_linker {
    const graph& G;bound_model& model;
    result out;
    std::unordered_map<cell_key,id,cell_hash,cell_equal> cells;
    std::unordered_map<id,id> bindings,cache,header_bindings;
    std::unordered_set<id> visiting;
    origin current_origin{};order current_order{};
    std::span<const endpoint_path_step> path(range r) const {return std::span<const endpoint_path_step>{model.paths}.subspan(r.begin,r.count);}
    [[noreturn]] void fail(const char* text) const {throw failure(text,current_origin,current_order);}
    type_ref root_type(identity_ref who) const {const auto* o=G.find(G.find_object(who));if(!o)fail("Canonical storage object missing");return o->type;}
    type_ref step(type_ref t,const endpoint_path_step& s) const {
        t=unqualified(G,t);derived_type_record d;type_handle record;
        if(s.kind==endpoint_path_step_kind::array_index){if(!G.derived(t,d)||d.kind!=derived_type_kind::bounded_array||s.value>=d.payload)fail("Canonical array path invalid");return d.child;}
        if(s.kind==endpoint_path_step_kind::dereference){type_ref child;if(!reference(G,t,&child))fail("Canonical dereference invalid");return child;}
        if(!G.named(t,record))fail("Canonical record path invalid");
        if(s.kind==endpoint_path_step_kind::base){const auto bb=G.bases(record);if(s.value>=bb.size()||bb[s.value].virtual_base())fail("Canonical base path invalid");return G.named(bb[s.value].type);}
        const auto mm=G.members(record);if(s.kind!=endpoint_path_step_kind::member||s.value>=mm.size())fail("Canonical member path invalid");return mm[s.value].type;
    }
    id intern(identity_ref who,type_ref t,range r){const cell_key key{who,r};if(const auto it=cells.find(key);it!=cells.end())return it->second;
        const auto n=static_cast<id>(out.cells.size());out.cells.push_back({who,t,r});cells.emplace(key,n);return n;}
    id intern(identity_ref who,type_ref t,const std::vector<endpoint_path_step>& pp){
        const auto begin=static_cast<id>(model.paths.size());model.paths.insert(model.paths.end(),pp.begin(),pp.end());
        const cell_key key{who,{begin,static_cast<id>(pp.size())}};
        if(const auto it=cells.find(key);it!=cells.end()){model.paths.resize(begin);return it->second;}
        return intern(who,t,key.steps);
    }
    bool has_dereference(range r) const {for(const auto& s:path(r))if(s.kind==endpoint_path_step_kind::dereference)return true;return false;}
    id canonical(const typed_endpoint& e,bool follow_final,unsigned depth=0){
        if(depth>256)fail("Reference chain too deep");
        if(!has_dereference(e.steps)){
            const auto c=intern(e.who,e.type,e.steps);
            return follow_final&&reference(G,e.type)?follow(c,depth+1):c;
        }
        auto who=e.who;auto type=e.root_type;std::vector<endpoint_path_step> prefix;
        // Copy input because recursion can append/reallocate model.paths.
        const std::vector<endpoint_path_step> input(path(e.steps).begin(),path(e.steps).end());
        for(const auto& s:input){
            if(s.kind==endpoint_path_step_kind::dereference){
                const auto resolved=follow(intern(who,type,prefix),depth+1);const auto c=out.cells[resolved];
                who=c.who;type=c.type;prefix.assign(path(c.steps).begin(),path(c.steps).end());
            }else{type=step(type,s);prefix.push_back(s);}
        }
        const auto c=intern(who,type,prefix);return follow_final&&reference(G,type)?follow(c,depth+1):c;
    }
    typed_endpoint default_source(id c){const auto ref=out.cells[c];auto type=root_type(ref.who);
        std::vector<endpoint_path_step> prefix;construction_value initial{};bool found=false;
        for(const auto& s:path(ref.steps)){
            if(s.kind==endpoint_path_step_kind::member){type_handle record;if(!G.named(unqualified(G,type),record))fail("Default reference owner invalid");
                const auto* entry=G.find(record);const auto constructions=G.member_construction_entries();
                if(!entry||s.value>=entry->members.count)fail("Default reference member invalid");
                const auto v=constructions[entry->members.begin+static_cast<std::size_t>(s.value)];
                const auto next=step(type,s);
                if(reference(G,next)){
                    initial=v;found=true;
                    if(v.kind==construction_kind::member_binding){const auto members=G.members(record);if(!v.operand || v.operand>members.size())fail("Default member binding invalid");
                        prefix.push_back({v.operand-1,endpoint_path_step_kind::member,{}});
                        const auto begin=static_cast<id>(model.paths.size());model.paths.insert(model.paths.end(),prefix.begin(),prefix.end());
                        return {ref.who,root_type(ref.who),members[v.operand-1].type,{begin,static_cast<id>(prefix.size())},current_origin};
                    }
                }
            }
            type=step(type,s);prefix.push_back(s);
        }
        if(ref.steps.count==0){const auto handle=G.find_object(ref.who);if(!G.construction(handle,initial))fail("Default object construction unavailable");found=true;}
        if(found && initial.kind==construction_kind::object_binding){const auto who=identity_ref::from_raw(initial.operand);
            return {who,root_type(who),root_type(who),{},current_origin};}
        fail("Unbound reference storage target");
    }
    id follow(id c,unsigned depth){
        if(depth>256)fail("Reference chain too deep");
        if(const auto it=cache.find(c);it!=cache.end())return it->second;
        if(!visiting.insert(c).second)fail("Reference cycle");
        ++out.reference_follows;id source;
        if(const auto it=bindings.find(c);it!=bindings.end())source=canonical(model.endpoints[model.bindings[it->second].source],true,depth+1);
        else if(const auto it=header_bindings.find(c);it!=header_bindings.end())source=canonical(model.endpoints[it->second],true,depth+1);
        else {++out.default_follows;const auto endpoint=default_source(c);source=canonical(endpoint,true,depth+1);}
        visiting.erase(c);cache[c]=source;return source;
    }
    void select(std::unordered_map<id,id>& into,id cell,id b){const auto it=into.find(cell);
        if(it==into.end() || model.bindings[it->second].event<model.bindings[b].event)into[cell]=b;
    }
    id import_endpoint(object_endpoint e){const auto rt=root_type(e.object);std::vector<endpoint_path_step> steps;
        if(e.member.is_member())steps.push_back({e.member.direct_member().value(),endpoint_path_step_kind::member,{}});
        else {const auto all=G.endpoint_path_entries();const auto n=e.member.path().value();if(!n||n>all.size())fail("Header endpoint path missing");
            const auto r=all[n-1].steps;const auto pp=G.endpoint_path_step_entries();steps.assign(pp.begin()+r.begin,pp.begin()+r.begin+r.count);}
        auto type=rt;for(const auto& s:steps)type=step(type,s);
        const auto begin=static_cast<id>(model.paths.size());model.paths.insert(model.paths.end(),steps.begin(),steps.end());
        model.endpoints.push_back({e.object,rt,type,{begin,static_cast<id>(steps.size())},{}});return static_cast<id>(model.endpoints.size()-1);
    }
    void link_bindings(){
        // Existing explicit Header links are defaults for the Source linking pass.
        for(const auto& link:G.link_entries()){
            const auto target=import_endpoint(link.target),source=import_endpoint(link.source);
            if(has_dereference(model.endpoints[target].steps))fail("Indirect Header binding target outside first slice");
            header_bindings[canonical(model.endpoints[target],false)]=source;
        }
        std::unordered_map<id,id> direct;std::vector<id> indirect;
        for(id b=0;b<model.bindings.size();++b){const auto& op=model.bindings[b];current_origin=op.at;current_order=op.event;
            if(has_dereference(model.endpoints[op.target].steps))indirect.push_back(b);
            else select(direct,canonical(model.endpoints[op.target],false),b);
        }
        bindings=direct;++out.link_rounds;
        if(!indirect.empty()){
            bool done=false;
            for(std::size_t round=0;round<=indirect.size()+1;++round){auto next=direct;cache.clear();visiting.clear();std::optional<failure> unresolved;
                for(auto b:indirect){const auto& op=model.bindings[b];current_origin=op.at;current_order=op.event;
                    try{select(next,canonical(model.endpoints[op.target],false),b);}catch(const failure& f){if(!unresolved)unresolved=f;cache.clear();visiting.clear();}}
                ++out.link_rounds;const bool stable=next==bindings;bindings=std::move(next);
                if(stable){if(unresolved)throw *unresolved;done=true;break;}
            }
            if(!done)fail("Nested reference target linking did not converge");
        }
        cache.clear();visiting.clear();
        std::vector<id> keys;for(auto [c,b]:bindings){(void)b;keys.push_back(c);}std::sort(keys.begin(),keys.end());
        for(auto c:keys){const auto b=bindings.at(c);current_origin=model.bindings[b].at;current_order=model.bindings[b].event;
            out.bindings.push_back({c,follow(c,0),b});}
        // Detect duplicates after targets, including nested target aliases, settle.
        std::unordered_map<id,id> heads;std::vector<id> next(model.bindings.size(),none);
        for(id b=0;b<model.bindings.size();++b){current_origin=model.bindings[b].at;current_order=model.bindings[b].event;
            const auto c=canonical(model.endpoints[model.bindings[b].target],false);const auto [it,inserted]=heads.emplace(c,b);
            if(!inserted){next[b]=it->second;it->second=b;}}
        std::vector<id> hh;
        for(auto [c,head]:heads){hh.clear();for(auto b=head;b!=none;b=next[b])hh.push_back(b);
            std::sort(hh.begin(),hh.end(),[&](id a,id b){return model.bindings[a].event<model.bindings[b].event;});
            for(std::size_t i=1;i<hh.size();++i){const auto& p=model.bindings[hh[i-1]];const auto& n=model.bindings[hh[i]];
                out.warnings.push_back({p.at,n.at,p.event,n.event,c,true});}}
    }
    void reduce_writes(){
        std::unordered_map<id,id> heads;std::vector<id> next(model.writes.size(),none);
        for(id w=0;w<model.writes.size();++w){const auto& op=model.writes[w];current_origin=op.at;current_order=op.event;
            const auto c=canonical(model.endpoints[op.target],false);const auto [it,inserted]=heads.emplace(c,w);
            if(!inserted){next[w]=it->second;it->second=w;}}
        std::vector<id> history;
        for(const auto& [c,head]:heads){history.clear();for(auto w=head;w!=none;w=next[w])history.push_back(w);
            std::sort(history.begin(),history.end(),[&](id a,id b){return model.writes[a].event<model.writes[b].event;});
            out.values.push_back({c,history.back()});
            for(std::size_t i=1;i<history.size();++i){const auto& p=model.writes[history[i-1]];const auto& n=model.writes[history[i]];
                out.warnings.push_back({p.at,n.at,p.event,n.event,c,false});}}
        std::sort(out.values.begin(),out.values.end(),[](auto a,auto b){return a.cell<b.cell;});
    }
public:
    alias_linker(const graph& g,bound_model& input):G(g),model(input),cells(0,cell_hash{&input.paths},cell_equal{&input.paths}){}
    result run(){link_bindings();reduce_writes();
        std::sort(out.warnings.begin(),out.warnings.end(),[](const auto& a,const auto& b){return std::tie(a.current_event,a.rebind,a.cell)<std::tie(b.current_event,b.rebind,b.cell);});
        return std::move(out);
    }
};
}
