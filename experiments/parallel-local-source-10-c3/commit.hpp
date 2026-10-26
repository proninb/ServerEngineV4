#pragma once
#include "../parallel-local-source-02/bind.hpp"
#include "../../server_engine/project/source/source_map.hpp"
#include "../../server_engine/project/parser/parser.hpp"
#include <deque>
#include "stats.hpp"
namespace pls10c3 {
using namespace pls02;
inline thread_local std::deque<std::string> details;
struct target {identity_ref who;type_ref root,type;std::vector<endpoint_path_step> steps;};
class commit {
    graph& G;source_map& sources;std::vector<parser_warning>* warnings;
    struct previous {origin at{};bool present=false;};
    std::vector<previous> values,links;
    origin current{};
    void status(server_status s){if(!succeeded(s))throw failure("Ordered Graph commit failed",current);}
    [[noreturn]] void fail(const char* s){throw failure(s,current);}
    type_ref root(identity_ref who){const auto* o=G.find(G.find_object(who));if(!o)fail("Object not registered");return o->type;}
    type_ref step(type_ref t,endpoint_path_step s){t=unqualified(G,t);derived_type_record d;type_handle h;
        if(s.kind==endpoint_path_step_kind::array_index){if(!G.derived(t,d)||d.kind!=derived_type_kind::bounded_array||s.value>=d.payload)fail("Invalid array target");return d.child;}
        if(!G.named(t,h))fail("Invalid record target");const auto* r=G.find(h);if(!r||r->record_kind==graph_record_kind::union_type)fail("Ordered slice excludes union storage");
        if(s.kind==endpoint_path_step_kind::base){auto b=G.bases(h);if(s.value>=b.size()||b[s.value].virtual_base())fail("Invalid base target");return G.named(b[s.value].type);}
        const auto m=G.members(h);if(s.kind!=endpoint_path_step_kind::member||s.value>=m.size())fail("Invalid member target");return m[s.value].type;
    }
    object_endpoint encode(const target& t){endpoint_ref ep;
        if(t.steps.size()==1&&t.steps[0].kind==endpoint_path_step_kind::member&&t.steps[0].value<=endpoint_path_handle::maximum_slot)
            ep=endpoint_ref::from_raw(static_cast<id>(t.steps[0].value));
        else {endpoint_path_handle p;status(G.intern_resolved_endpoint_path(t.root,t.steps,t.type,p));ep=endpoint_ref::from_path(p);}return {t.who,ep};
    }
    target decode(object_endpoint ep){target t{ep.object,root(ep.object),{}, {}};t.type=t.root;
        if(ep.member.is_member())t.steps.push_back({ep.member.direct_member().value(),endpoint_path_step_kind::member,{}});
        else {endpoint_path_record p;if(!G.endpoint_path(ep.member.path(),p))fail("Binding path absent");auto s=G.endpoint_path_steps(ep.member.path());t.steps.assign(s.begin(),s.end());}
        for(auto s:t.steps)t.type=step(t.type,s);return t;
    }
    target default_target(const target& t){auto type=t.root;std::vector<endpoint_path_step> prefix;construction_value v{};bool found=false;
        for(auto s:t.steps){if(s.kind==endpoint_path_step_kind::member){type_handle h;if(!G.named(unqualified(G,type),h))fail("Default owner invalid");
                const auto* record=G.find(h);const auto members=G.members(h);if(!record||s.value>=members.size())fail("Default member absent");
                const auto* initial=G.construction(h,endpoint_ref::from_raw(static_cast<id>(s.value)).direct_member());
                if(reference(G,members[s.value].type)&&initial){v=*initial;found=true;
                    if(v.kind==construction_kind::member_binding){if(!v.operand||v.operand>members.size())fail("Default member binding invalid");prefix.push_back({v.operand-1,endpoint_path_step_kind::member,{}});return {t.who,t.root,members[v.operand-1].type,std::move(prefix)};}}}
            type=step(type,s);prefix.push_back(s);}
        if(t.steps.empty()){found=G.construction(G.find_object(t.who),v);}
        if(found&&v.kind==construction_kind::object_binding){auto who=identity_ref::from_raw(v.operand);auto rt=root(who);return {who,rt,rt,{}};}
        fail("Reference is unbound at this Source operation");
    }
    target follow(target t,unsigned depth){if(depth>256)fail("Reference cycle or chain too deep");
        const auto h=G.link_at_target(encode(t));if(h){const auto* l=G.find(h);if(!l)fail("Binding missing");t=decode(l->source);}else t=default_target(t);
        return reference(G,t.type)?follow(std::move(t),depth+1):t;
    }
    void warn(origin previous_origin){++conflicts;if(!warnings&&!std::getenv("PLS10_TRACE"))return;
        details.push_back("Source cell overwritten; previous file="+std::to_string(previous_origin.file.value())+" offset="+std::to_string(previous_origin.source.offset)+" length="+std::to_string(previous_origin.source.length)+"; current file="+std::to_string(current.file.value())+" offset="+std::to_string(current.source.offset)+" length="+std::to_string(current.source.length));
        if(std::getenv("PLS10_TRACE"))trace.push_back(details.back());
        if(warnings)warnings->push_back({parser_warning_kind::duplicate_initialization,current.file,current.source,details.back()});
    }
    static server_status decide(void* p,std::uint32_t slot,bool,bool& accept)noexcept{
        auto& self=*static_cast<commit*>(p);try{if(slot>=self.values.size())self.values.resize(std::size_t(slot)+1);
            auto& old=self.values[slot];if(old.present)self.warn(old.at);old={self.current,true};accept=true;return server_status::success;
        }catch(...){return server_status::io_error;}
    }
public:
    std::uint64_t writes=0,bindings=0,follow_calls=0;
    commit(graph& g,source_map& s,std::vector<parser_warning>* w):G(g),sources(s),warnings(w){details.clear();trace.clear();conflicts=0;
        // Existing seed keys must obey the same single-member encoding. Do not
        // silently miss an older binding/value under an alternate raw key.
        const auto check=[&](object_endpoint ep){if(ep.member.is_path()){const auto p=G.endpoint_path_steps(ep.member.path());
            if(p.size()==1&&p[0].kind==endpoint_path_step_kind::member)fail("Noncanonical seed endpoint outside ordered slice");}};
        for(const auto& link:G.link_entries())check(link.target);
        for(const auto& value:G.initialization_entries())check(value.target);
    }
    void at(origin o){current=o;}
    target resolve(const typed_endpoint& e,const std::vector<endpoint_path_step>& paths,bool final=false){
        target t{e.who,e.root_type,e.root_type,{}};
        for(auto s:std::span<const endpoint_path_step>{paths}.subspan(e.steps.begin,e.steps.count)){
            if(s.kind==endpoint_path_step_kind::dereference){++follow_calls;t=follow(std::move(t),0);}else {t.type=step(t.type,s);t.steps.push_back(s);}}
        return final&&reference(G,t.type)?follow(std::move(t),0):t;
    }
    void declaration(identity_ref who){status(sources.add(current.file,source_data_ref::object(who)));}
    void write(const typed_endpoint& e,const std::vector<endpoint_path_step>& paths,construction_value value,bool declaration_initializer){
        at(e.at);auto t=resolve(e,paths);const auto ep=encode(t);auto type=t.type;derived_type_record d;bool immutable=false;
        while(G.derived(type,d)&&(d.kind==derived_type_kind::const_qualified||d.kind==derived_type_kind::volatile_qualified)){immutable|=d.kind==derived_type_kind::const_qualified;type=d.child;}
        if(immutable){construction_value cold;if(!declaration_initializer||!t.steps.empty()||!G.construction(G.find_object(t.who),cold)||cold!=value)fail("Invalid const write");}
        else {initialization_apply_result output;status(G.apply_resolved_initialization(ep,t.type,value,decide,this,output));status(sources.add_initialization(ep));}
        status(sources.add(current.file,source_data_ref::object(t.who)));++writes;
    }
    void bind(const typed_endpoint& lhs,const typed_endpoint& rhs,const std::vector<endpoint_path_step>& paths){
        at(lhs.at);const auto t=resolve(lhs,paths),s=resolve(rhs,paths,true);type_ref child;
        if(!reference(G,t.type,&child)||child!=s.type)fail("Reference link types differ");
        link_handle link;bool replaced=false;status(G.rebind_resolved_link(encode(s),encode(t),link,replaced));
        if(link.value()>links.size())links.resize(link.value());auto& old=links[link.value()-1];if(old.present)warn(old.at);old={current,true};
        status(sources.add(current.file,source_data_ref::link(link)));++bindings;
    }
};
}
