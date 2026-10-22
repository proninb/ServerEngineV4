#pragma once
#include "../../server_engine/project/graph/construction_semantics.hpp"
namespace sc02 {
using namespace sb01;
inline bool compatible(bound_view type,CV value){
    if(!cw::server::valid_construction(value))return false;
    for(auto i=type.layers.rbegin();i!=type.layers.rend();++i){
        if(i->kind==D::const_qualified||i->kind==D::volatile_qualified)continue;
        if(i->kind==D::pointer)return value.kind==CK::zero||((value.kind==CK::unsigned_integer||value.kind==CK::signed_integer)&&value.bits()==0);
        if(i->kind==D::lvalue_reference||i->kind==D::rvalue_reference)return value.kind==CK::zero;
        return i->kind==D::bounded_array&&i->payload!=0&&value.kind==CK::zero;
    }
    if(type.entity!=none)return value.kind==CK::zero;
    if(type.builtin==I::none||type.builtin==I::void_type)return false;
    if(type.builtin==I::nullptr_type)return value.kind==CK::zero||((value.kind==CK::unsigned_integer||value.kind==CK::signed_integer)&&value.bits()==0);
    if(cw::server::construction_semantics_detail::floating_intrinsic(type.builtin))return value.kind==CK::zero||value.kind==CK::real||value.kind==CK::signed_integer||value.kind==CK::unsigned_integer;
    return cw::server::construction_semantics_detail::integral_intrinsic(type.builtin)&&(value.kind==CK::zero||value.kind==CK::signed_integer||value.kind==CK::unsigned_integer);
}
struct action {event at;id request=none;const operation* op=nullptr;};
struct waiting_reference {id target;std::uint64_t generation;origin source;};
inline bool is_reference(bound_view t){return !t.layers.empty()&&(t.layers.back().kind==D::lvalue_reference||t.layers.back().kind==D::rvalue_reference);}
inline bool referent_matches(bound_view target,bound_view source){
    if(!is_reference(target))return false;
    target.layers=target.layers.first(target.layers.size()-1);
    if(is_reference(source))source.layers=source.layers.first(source.layers.size()-1);
    return equal(target,source);
}
inline void resolve(const batch& b,const ca01::result& canonical,binding_result& result){
    std::vector<std::vector<action>> actions(b.declarations.size());
    std::vector<bool> gaps(b.declarations.size(),false),needs_fields(b.declarations.size(),false);
    for(const auto& op:b.operations){
        if(op.kind==operation_kind::gap)gaps[op.definition]=true;
        if(op.kind==operation_kind::scalar||op.kind==operation_kind::reference)needs_fields[op.definition]=true;
        actions[op.definition].push_back({op.at,none,&op});
    }
    b.requests.range(0,b.requests.size(),[&](auto i,const request& r){
        if(!r.base&&needs_fields[r.definition]&&!gaps[r.definition])actions[r.definition].push_back({r.at+1,static_cast<id>(i),nullptr});
    });
    std::unordered_map<id,std::vector<std::pair<std::string_view,CV>>> definitions;
    for(id di=0;di<b.declarations.size();++di){const auto& d=b.declarations[di];if(d.kind!=declaration_kind::record)continue;
        if(gaps[di]){++result.gap_definitions;for(const auto& e:actions[di])if(e.op&&e.op->kind==operation_kind::reference)++result.skipped_reference_events;continue;}
        if(d.complete!=UINT64_MAX)++result.covered_definitions;
        auto& events=actions[di];std::sort(events.begin(),events.end(),[](auto a,auto z){return a.at<z.at;});
        std::unordered_map<std::string_view,id> fields;
        std::vector<id> members;
        std::unordered_map<std::string_view,const operation*> pending;
        std::unordered_map<id,CV> values;
        std::unordered_map<id,std::uint64_t> generations;
        std::unordered_multimap<std::string_view,waiting_reference> waiting;
        bool constructor_seen=false,stopped=false;
        auto failure=[&](event at,origin source,std::string_view detail,id kind=2){earliest(result.first,{error_kind::coverage,at,source,kind,detail});stopped=true;};
        auto apply=[&](id target,const operation& op,event decision){
            const auto request=members[target];auto revision=generations.find(target);
            if(op.kind!=operation_kind::reference){
                if(!compatible(result.types[request],op.value)){failure(decision,op.source,"Header Parser V2 construction value is incompatible with its field");return;}
                if(revision!=generations.end())++revision->second;
                values[request]=op.value;return;
            }
            if(!is_reference(result.types[request])){
                failure(decision,op.reference_source,"Value construction requires a scalar constant, not a member name",3);return;
            }
            const auto source=fields.find(op.reference);
            if(source==fields.end()){
                auto [it,inserted]=generations.try_emplace(target,0);(void)inserted;++it->second;
                waiting.emplace(op.reference,waiting_reference{target,it->second,op.reference_source});
                values[request]={};++result.queued_references;return;
            }
            if(!referent_matches(result.types[request],result.types[members[source->second]])){
                failure(decision,op.reference_source,"Reference binding type does not match bound member type");return;
            }
            if(revision!=generations.end())++revision->second;
            values[request]=CV::member_binding(source->second+1);++result.immediate_references;
        };
        for(const auto& e:events){if(stopped)break;
            if(e.op){const auto& op=*e.op;
                if(op.kind==operation_kind::begin){++result.constructors;if(constructor_seen)failure(op.at,op.source,"Header Parser V2 supports one managed default constructor per record");constructor_seen=true;}
                else if(op.kind==operation_kind::scalar||op.kind==operation_kind::reference){
                    if(op.kind==operation_kind::scalar)++result.scalar_operations;else ++result.reference_events;
                    const auto target=fields.find(op.target);
                    if(target==fields.end()){
                        const auto old=pending.find(op.target);if(old!=pending.end()&&old->second->kind==operation_kind::reference)++result.superseded_reference_targets;
                        pending[op.target]=&op;if(op.kind==operation_kind::reference)++result.pending_reference_targets;
                    }else apply(target->second,op,op.at);
                }
            }else{
                const auto& r=b.requests[e.request];const auto ordinal=static_cast<id>(members.size());fields.emplace(r.member,ordinal);members.push_back(e.request);
                const auto found=pending.find(r.member);if(found!=pending.end()){apply(ordinal,*found->second,e.at);pending.erase(found);}
                if(stopped)break;
                // Header V2 validates type compatibility before generation.
                // A stale entry may therefore still produce the first error.
                const auto range=waiting.equal_range(r.member);
                for(auto it=range.first;it!=range.second;){const auto item=it->second;
                    if(!referent_matches(result.types[members[item.target]],result.types[e.request])){
                        failure(e.at,item.source,"Reference binding type does not match bound member type");break;
                    }
                    const auto current=generations.find(item.target);ca01::require(current!=generations.end(),"missing reference generation");
                    if(current->second==item.generation){values[members[item.target]]=CV::member_binding(ordinal+1);++result.resolved_references;}
                    else ++result.stale_references;
                    it=waiting.erase(it);
                }
            }
        }
        if(!stopped&&d.complete!=UINT64_MAX){
            if(!pending.empty()){auto first=pending.begin();for(auto it=pending.begin();it!=pending.end();++it)if(it->second->source.offset<first->second->source.offset)first=it;
                failure(d.complete,first->second->source,"Header Parser V2 constructor target is not a field of this record");}
            else if(!waiting.empty()){auto first=waiting.begin();for(auto it=waiting.begin();it!=waiting.end();++it)if(it->second.source.offset<first->second.source.offset)first=it;
                failure(d.complete,first->second.source,"Reference binding names neither a member nor a visible Header static object");}
        }
        std::vector<std::pair<std::string_view,CV>> normalized;
        for(auto [request,value]:values)if(value.kind!=CK::zero){result.defaults.push_back({request,value});normalized.emplace_back(b.requests[request].member,value);}
        std::sort(normalized.begin(),normalized.end(),[](const auto& x,const auto& y){return x.first<y.first;});
        if(!stopped&&d.complete!=UINT64_MAX){auto [it,inserted]=definitions.try_emplace(canonical.representative[d.symbol],std::move(normalized));
            if(!inserted&&it->second!=normalized)failure(d.complete,d.complete_source,"Header Parser V2 record definition conflicts with G");}
    }
    std::sort(result.defaults.begin(),result.defaults.end(),[](auto a,auto z){return a.request<z.request;});
}
}
