#pragma once
#include "../../server_engine/project/graph/construction_semantics.hpp"
namespace sc01 {
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
inline void resolve(const batch& b,const ca01::result& canonical,binding_result& result){
    std::vector<std::vector<action>> actions(b.declarations.size());std::vector<bool> gaps(b.declarations.size(),false),needs_fields(b.declarations.size(),false);
    for(const auto& op:b.operations){if(op.kind==operation_kind::gap)gaps[op.definition]=true;if(op.kind==operation_kind::scalar)needs_fields[op.definition]=true;actions[op.definition].push_back({op.at,none,&op});}
    b.requests.range(0,b.requests.size(),[&](auto i,const request& r){if(!r.base&&needs_fields[r.definition]&&!gaps[r.definition])actions[r.definition].push_back({r.at+1,static_cast<id>(i),nullptr});});
    std::unordered_map<id,std::vector<std::pair<std::string_view,CV>>> definitions;
    for(id di=0;di<b.declarations.size();++di){const auto& d=b.declarations[di];if(d.kind!=declaration_kind::record)continue;
        if(gaps[di]){++result.gap_definitions;continue;}if(d.complete!=UINT64_MAX)++result.covered_definitions;
        auto& events=actions[di];std::sort(events.begin(),events.end(),[](auto a,auto z){return a.at<z.at;});
        std::unordered_map<std::string_view,id> fields;std::unordered_map<std::string_view,const operation*> pending;std::unordered_map<id,CV> values;bool constructor_seen=false;
        auto failure=[&](event at,origin source,std::string_view detail){earliest(result.first,{error_kind::coverage,at,source,2,detail});};
        auto apply=[&](id target,const operation& op,event decision){
            if(!compatible(result.types[target],op.value))failure(decision,op.source,"Header Parser V2 construction value is incompatible with its field");else values[target]=op.value;
        };
        for(const auto& e:events){
            if(e.op){const auto& op=*e.op;
                if(op.kind==operation_kind::begin){++result.constructors;if(constructor_seen)failure(op.at,op.source,"Header Parser V2 supports one managed default constructor per record");constructor_seen=true;}
                else if(op.kind==operation_kind::scalar){++result.scalar_operations;const auto found=fields.find(op.target);if(found==fields.end())pending[op.target]=&op;else apply(found->second,op,op.at);}
            }else{const auto& r=b.requests[e.request];fields.emplace(r.member,e.request);const auto found=pending.find(r.member);if(found!=pending.end()){apply(e.request,*found->second,e.at);pending.erase(found);}}
        }
        if(!pending.empty()&&d.complete!=UINT64_MAX){auto first=pending.begin();for(auto it=pending.begin();it!=pending.end();++it)if(it->second->source.offset<first->second->source.offset)first=it;
            failure(d.complete,first->second->source,"Header Parser V2 constructor target is not a field of this record");}
        std::vector<std::pair<std::string_view,CV>> normalized;
        for(auto [request,value]:values)if(value.kind!=CK::zero){result.defaults.push_back({request,value});normalized.emplace_back(b.requests[request].member,value);}
        std::sort(normalized.begin(),normalized.end(),[](const auto& x,const auto& y){return x.first<y.first;});
        if(d.complete!=UINT64_MAX){auto [it,inserted]=definitions.try_emplace(canonical.representative[d.symbol],std::move(normalized));
            if(!inserted&&it->second!=normalized)failure(d.complete,d.complete_source,"Header Parser V2 record definition conflicts with G");}
    }
    std::sort(result.defaults.begin(),result.defaults.end(),[](auto a,auto z){return a.request<z.request;});
}
}
