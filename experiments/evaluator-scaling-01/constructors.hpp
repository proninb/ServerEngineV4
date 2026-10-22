#pragma once
#include <map>
#include "audit.hpp"
#include "../../server_engine/project/graph/construction_semantics.hpp"
namespace ea01 {
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
    if(!active||!active->mode){sc02::resolve(b,canonical,result);return;}
    active->start();ea01::scope entire(ea01::other,false);
    std::vector<std::vector<action>> actions(b.declarations.size());
    std::vector<bool> gaps(b.declarations.size(),false),needs_fields(b.declarations.size(),false);
    {ea01::scope stage(ea01::actions,false);
    for(const auto& op:b.operations){
        if(op.kind==operation_kind::gap)gaps[op.definition]=true;
        if(op.kind==operation_kind::nested||op.kind==operation_kind::scalar||op.kind==operation_kind::reference)needs_fields[op.definition]=true;
        actions[op.definition].push_back({op.at,none,&op});
    }
    b.requests.range(0,b.requests.size(),[&](auto i,const request& r){
        if(!r.base&&needs_fields[r.definition]&&!gaps[r.definition])actions[r.definition].push_back({r.at+1,static_cast<id>(i),nullptr});
    });
    }
    struct schema {event complete;id first=none,last=0;std::unordered_map<std::string_view,id> members;bool indexed=false;};
    std::unordered_map<id,schema> schemas;
    std::unordered_map<id,id> schema_definitions;
    {ea01::scope stage(ea01::catalog,false);
    if(nested_coverage_enabled)for(id di=0;di<b.declarations.size();++di){const auto& d=b.declarations[di];if(d.kind!=declaration_kind::record||d.complete==UINT64_MAX)continue;const auto rep=canonical.representative[d.symbol];auto it=schemas.find(rep);if(it==schemas.end()||d.complete<it->second.complete){schemas.insert_or_assign(rep,schema{d.complete});schema_definitions[rep]=di;}}
    if(nested_coverage_enabled)b.requests.range(0,b.requests.size(),[&](auto i,const request& r){if(r.base)return;const auto rep=canonical.representative[r.owner];auto it=schema_definitions.find(rep);if(it!=schema_definitions.end()&&it->second==r.definition){auto& schema=schemas.at(rep);schema.first=(std::min)(schema.first,static_cast<id>(i));schema.last=static_cast<id>(i+1);}});
    }
    std::map<std::pair<id,std::string>,CV> committed_nested;
    std::unordered_map<id,std::vector<std::pair<std::string_view,CV>>> definitions;
    for(id di=0;di<b.declarations.size();++di){const auto& d=b.declarations[di];if(d.kind!=declaration_kind::record)continue;
        ea01::record_scope record_clock(di,actions[di].size());
        if(gaps[di]){++result.gap_definitions;for(const auto& e:actions[di])if(e.op&&e.op->kind==operation_kind::reference)++result.skipped_reference_events;continue;}
        if(d.complete!=UINT64_MAX)++result.covered_definitions;
        auto& events=actions[di];{ea01::scope timer(ea01::record_setup);std::sort(events.begin(),events.end(),[](auto a,auto z){return a.at<z.at;});}
        ea01::scope cleanup_timer;
        ea01::scope state_timer(ea01::state_construction);
        std::unordered_map<std::string_view,id> fields;
        std::vector<id> members;
        std::unordered_map<std::string_view,const operation*> pending;
        std::unordered_map<id,CV> values;
        std::unordered_map<id,std::uint64_t> generations;
        std::unordered_multimap<std::string_view,waiting_reference> waiting;
        std::unordered_map<std::string_view,std::vector<const operation*>> pending_nested;
        std::map<std::string,CV> completed_nested;
        bool constructor_seen=false,stopped=false;
        state_timer.stop();
        auto failure=[&](event at,origin source,std::string_view detail,id kind=2){earliest(result.first,{error_kind::coverage,at,source,kind,detail});stopped=true;};
        auto apply_nested=[&](id target,const operation& op,event decision){ea01::scope timer(ea01::nested);
            auto type=result.types[members[target]];std::string spelling(op.target);
            for(id i=0;i<op.path.count;++i){const auto& step=op.path.arena->steps[op.path.offset+i];
                if(!step.name.empty()){
                    if(type.entity==none||!type.layers.empty()){failure(decision,op.source,"Nested constructor path requires direct record fields",3);return;}
                    if(active->mode>=3&&!active->schema_accesses.empty()){++active->schema_accesses[type.entity];active->schema_users.emplace_back(di,type.entity);}
                    const auto schema=EA_EXPR(lookup,schemas.find(type.entity));
                    if(schema==schemas.end()||schema->second.complete>decision){failure(decision,op.source,"Nested constructor target record is invalid");return;}
                    if(!schema->second.indexed){if(active->mode>=3&&!active->schema_builds.empty())++active->schema_builds[type.entity];ea01::scope index_timer(ea01::index_build);auto& entry=schema->second;for(id i=entry.first;i<entry.last;++i){const auto& r=b.requests[i];if(!r.base&&r.definition==schema_definitions.at(type.entity))entry.members.emplace(r.member,i);}entry.indexed=true;}
                    const auto field=EA_EXPR(lookup,schema->second.members.find(step.name));
                    if(field==schema->second.members.end()){failure(decision,op.source,"Nested constructor field does not exist");return;}
                    type=result.types[field->second];spelling+='.';spelling+=step.name;
                }else{
                    if(type.layers.empty()||type.layers.back().kind!=D::bounded_array){failure(decision,op.source,"Constructor array index requires a bounded array");return;}
                    if(step.index>=type.layers.back().payload){failure(decision,op.source,"Constructor array index is outside the declared bound");return;}
                    type.layers=type.layers.first(type.layers.size()-1);spelling+='[';spelling+=std::to_string(step.index);spelling+=']';
                }
            }
            if(type.entity!=none||!type.layers.empty()||!EA_EXPR(compatibility,compatible(type,op.value))){failure(decision,op.source,"Nested constructor assignment requires a writable scalar field and scalar constant",3);return;}
            {ea01::scope nested_output_timer(ea01::output);completed_nested[std::move(spelling)]=op.value;}
        };
        auto apply=[&](id target,const operation& op,event decision){
            const auto request=members[target];auto revision=EA_EXPR(pending,generations.find(target));
            if(op.kind!=operation_kind::reference){
                if(!EA_EXPR(compatibility,compatible(result.types[request],op.value))){failure(decision,op.source,"Header Parser V2 construction value is incompatible with its field");return;}
                if(revision!=generations.end()){ea01::scope pending_timer(ea01::pending);++revision->second;}
                {ea01::scope timer(ea01::output);values[request]=op.value;}return;
            }
            if(!EA_EXPR(compatibility,is_reference(result.types[request]))){
                failure(decision,op.reference_source,"Value construction requires a scalar constant, not a member name",3);return;
            }
            const auto source=EA_EXPR(lookup,fields.find(op.reference));
            if(source==fields.end()){
                auto [it,inserted]=EA_EXPR(pending,generations.try_emplace(target,0));(void)inserted;++it->second;
                EA_EXPR(pending,waiting.emplace(op.reference,waiting_reference{target,it->second,op.reference_source}));
                values[request]={};++result.queued_references;return;
            }
            if(!EA_EXPR(compatibility,referent_matches(result.types[request],result.types[members[source->second]]))){
                failure(decision,op.reference_source,"Reference binding type does not match bound member type");return;
            }
            if(revision!=generations.end()){ea01::scope pending_timer(ea01::pending);++revision->second;}
            {ea01::scope timer(ea01::output);values[request]=CV::member_binding(source->second+1);}++result.immediate_references;
        };
        for(const auto& e:events){if(stopped)break;
            if(e.op){const auto& op=*e.op;
                if(op.kind==operation_kind::begin){++result.constructors;if(constructor_seen)failure(op.at,op.source,"Header Parser V2 supports one managed default constructor per record");constructor_seen=true;}
                else if(op.kind==operation_kind::nested){const auto target=EA_EXPR(lookup,fields.find(op.target));if(target==fields.end())EA_EXPR(pending,pending_nested[op.target].push_back(&op));else apply_nested(target->second,op,op.at);}
                else if(op.kind==operation_kind::scalar||op.kind==operation_kind::reference){
                    if(op.kind==operation_kind::scalar)++result.scalar_operations;else ++result.reference_events;
                    const auto target=EA_EXPR(lookup,fields.find(op.target));
                    if(target==fields.end()){
                        const auto old=EA_EXPR(pending,pending.find(op.target));if(old!=pending.end()&&old->second->kind==operation_kind::reference)++result.superseded_reference_targets;
                        {ea01::scope pending_timer(ea01::pending);pending[op.target]=&op;}if(op.kind==operation_kind::reference)++result.pending_reference_targets;
                    }else apply(target->second,op,op.at);
                }
            }else{ea01::scope field_timer(ea01::record_setup);
                const auto& r=b.requests[e.request];const auto ordinal=static_cast<id>(members.size());EA_EXPR(index_build,fields.emplace(r.member,ordinal));members.push_back(e.request);
                const auto found=EA_EXPR(pending,pending.find(r.member));if(found!=pending.end()){apply(ordinal,*found->second,e.at);pending.erase(found);}
                if(stopped)break;
                // Header V2 validates type compatibility before generation.
                // A stale entry may therefore still produce the first error.
                const auto range=EA_EXPR(pending,waiting.equal_range(r.member));
                for(auto it=range.first;it!=range.second;){const auto item=it->second;
                    if(!EA_EXPR(compatibility,referent_matches(result.types[members[item.target]],result.types[e.request]))){
                        failure(e.at,item.source,"Reference binding type does not match bound member type");break;
                    }
                    const auto current=EA_EXPR(pending,generations.find(item.target));ca01::require(current!=generations.end(),"missing reference generation");
                    if(current->second==item.generation){values[members[item.target]]=CV::member_binding(ordinal+1);++result.resolved_references;}
                    else ++result.stale_references;
                    it=EA_EXPR(pending,waiting.erase(it));
                }
                if(!stopped){auto nested=EA_EXPR(pending,pending_nested.find(r.member));if(nested!=pending_nested.end()){for(const auto* op:nested->second){apply_nested(ordinal,*op,e.at);if(stopped)break;}pending_nested.erase(nested);}}
            }
        }
        if(!stopped&&d.complete!=UINT64_MAX){
            if(!pending.empty()){auto first=pending.begin();for(auto it=pending.begin();it!=pending.end();++it)if(it->second->source.offset<first->second->source.offset)first=it;
                failure(d.complete,first->second->source,"Header Parser V2 constructor target is not a field of this record");}
            else if(!pending_nested.empty()){const operation* first=nullptr;for(const auto& [name,ops]:pending_nested)for(const auto* op:ops)if(!first||op->source.offset<first->source.offset)first=op;failure(d.complete,first->source,"Constructor target is not a field of this record");}
            else if(!waiting.empty()){auto first=waiting.begin();for(auto it=waiting.begin();it!=waiting.end();++it)if(it->second.source.offset<first->second.source.offset)first=it;
                failure(d.complete,first->second.source,"Reference binding names neither a member nor a visible Header static object");}
        }
        std::vector<std::pair<std::string_view,CV>> normalized;
        {ea01::scope timer(ea01::output);
        for(auto [request,value]:values)if(value.kind!=CK::zero){result.defaults.push_back({request,value});normalized.emplace_back(b.requests[request].member,value);}
        std::sort(normalized.begin(),normalized.end(),[](const auto& x,const auto& y){return x.first<y.first;});
        }
        {ea01::scope timer(ea01::merge);
        if(!stopped&&d.complete!=UINT64_MAX){auto [it,inserted]=definitions.try_emplace(canonical.representative[d.symbol],std::move(normalized));
            if(!inserted&&it->second!=normalized)failure(d.complete,d.complete_source,"Header Parser V2 record definition conflicts with G");}
        if(!stopped&&d.complete!=UINT64_MAX)for(const auto& [path,value]:completed_nested){auto [it,inserted]=committed_nested.emplace(std::make_pair(canonical.representative[d.symbol],path),value);if(!inserted&&it->second!=value){failure(d.complete,d.complete_source,"Nested constructor default conflicts with Graph");break;}}
        }
        cleanup_timer.start(ea01::state_cleanup);
    }
    ea01::scope final_timer(ea01::finalize,false);
    for(const auto& [key,value]:committed_nested)result.nested.push_back({key.first,key.second,value});
    std::sort(result.defaults.begin(),result.defaults.end(),[](auto a,auto z){return a.request<z.request;});
}
}
