#pragma once
#include "../evaluator-isolation-01/isolation.hpp"
#include "lazy_container.hpp"
namespace sl01 {
using namespace ei01;
inline bool enabled=true;
inline measurements last;
inline record_result evaluate_one(const batch& b,const bound_storage& types,const prepared& p,const ca01::result& canonical,id di){
    record_result out;const auto& d=b.declarations[di];out.definition=di;out.owner=canonical.representative[d.symbol];out.begin=d.begin;out.complete=d.complete;out.source=d.source;out.complete_source=d.complete_source;
    const auto& events=p.actions[di];
    if(p.gaps[di]){++out.count.gap_definitions;for(const auto& e:events)if(e.op&&e.op->kind==operation_kind::reference)++out.count.skipped_reference_events;return out;}
    if(d.complete!=UINT64_MAX)++out.count.covered_definitions;
        lazy_container<std::unordered_map<std::string_view,id>> fields;
        std::vector<id> members;
        lazy_container<std::unordered_map<std::string_view,const operation*>> pending;
        lazy_container<std::unordered_map<id,value_cell>> values;
        lazy_container<std::unordered_map<id,std::uint64_t>> generations;
        struct local_waiting {id target;std::uint64_t generation;origin source;event assignment;};
        lazy_container<std::unordered_multimap<std::string_view,local_waiting>> waiting;
        lazy_container<std::unordered_map<std::string_view,std::vector<const operation*>>> pending_nested;
        lazy_container<std::map<std::string,value_cell>> completed_nested;
        bool constructor_seen=false,stopped=false;
        auto failure=[&](event at,origin source,std::string_view detail,id kind=2){earliest(out.first,{error_kind::coverage,at,source,kind,detail});stopped=true;};
        auto apply_nested=[&](id target,const operation& op,event decision){
            auto type=types[members[target]];std::string spelling(op.target);
            for(id i=0;i<op.path.count;++i){const auto& step=op.path.arena->steps[op.path.offset+i];
                if(!step.name.empty()){
                    if(type.entity==none||!type.layers.empty()){failure(decision,op.source,"Nested constructor path requires direct record fields",3);return;}
                    const auto schema=p.schemas.find(type.entity);
                    if(schema==p.schemas.end()||schema->second.complete>decision){failure(decision,op.source,"Nested constructor target record is invalid");return;}
                    ca01::require(schema->second.indexed,"unprepared nested schema");
                    const auto field=schema->second.members.find(step.name);
                    if(field==schema->second.members.end()){failure(decision,op.source,"Nested constructor field does not exist");return;}
                    type=types[field->second];spelling+='.';spelling+=step.name;
                }else{
                    if(type.layers.empty()||type.layers.back().kind!=D::bounded_array){failure(decision,op.source,"Constructor array index requires a bounded array");return;}
                    if(step.index>=type.layers.back().payload){failure(decision,op.source,"Constructor array index is outside the declared bound");return;}
                    type.layers=type.layers.first(type.layers.size()-1);spelling+='[';spelling+=std::to_string(step.index);spelling+=']';
                }
            }
            if(type.entity!=none||!type.layers.empty()||!compatible(type,op.value)){failure(decision,op.source,"Nested constructor assignment requires a writable scalar field and scalar constant",3);return;}
            completed_nested[std::move(spelling)]={op.value,{op.at,decision,op.source}};
        };
        auto apply=[&](id target,const operation& op,event decision){
            const auto request=members[target];auto revision=generations.find(target);
            if(op.kind!=operation_kind::reference){
                if(!compatible(types[request],op.value)){failure(decision,op.source,"Header Parser V2 construction value is incompatible with its field");return;}
                if(revision!=generations.end())++revision->second;
                values[request]={op.value,{op.at,decision,op.source}};return;
            }
            if(!is_reference(types[request])){
                failure(decision,op.reference_source,"Value construction requires a scalar constant, not a member name",3);return;
            }
            const auto source=fields.find(op.reference);
            if(source==fields.end()){
                auto [it,inserted]=generations.try_emplace(target,0);(void)inserted;++it->second;
                waiting.emplace(op.reference,local_waiting{target,it->second,op.reference_source,op.at});
                values[request]={{},{op.at,decision,op.reference_source}};++out.count.queued_references;return;
            }
            if(!referent_matches(types[request],types[members[source->second]])){
                failure(decision,op.reference_source,"Reference binding type does not match bound member type");return;
            }
            if(revision!=generations.end())++revision->second;
            values[request]={CV::member_binding(source->second+1),{op.at,decision,op.reference_source}};++out.count.immediate_references;
        };
        for(const auto& e:events){if(stopped)break;
            if(e.op){const auto& op=*e.op;
                if(op.kind==operation_kind::begin){++out.count.constructors;if(constructor_seen)failure(op.at,op.source,"Header Parser V2 supports one managed default constructor per record");constructor_seen=true;}
                else if(op.kind==operation_kind::nested){const auto target=fields.find(op.target);if(target==fields.end())pending_nested[op.target].push_back(&op);else apply_nested(target->second,op,op.at);}
                else if(op.kind==operation_kind::scalar||op.kind==operation_kind::reference){
                    if(op.kind==operation_kind::scalar)++out.count.scalar_operations;else ++out.count.reference_events;
                    const auto target=fields.find(op.target);
                    if(target==fields.end()){
                        const auto old=pending.find(op.target);if(old!=pending.end()&&old->second->kind==operation_kind::reference)++out.count.superseded_reference_targets;
                        pending[op.target]=&op;if(op.kind==operation_kind::reference)++out.count.pending_reference_targets;
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
                    if(!referent_matches(types[members[item.target]],types[e.request])){
                        failure(e.at,item.source,"Reference binding type does not match bound member type");break;
                    }
                    const auto current=generations.find(item.target);ca01::require(current!=generations.end(),"missing reference generation");
                    if(current->second==item.generation){values[members[item.target]]={CV::member_binding(ordinal+1),{item.assignment,e.at,item.source}};++out.count.resolved_references;}
                    else ++out.count.stale_references;
                    it=waiting.erase(it);
                }
                if(!stopped){auto nested=pending_nested.find(r.member);if(nested!=pending_nested.end()){for(const auto* op:nested->second){apply_nested(ordinal,*op,e.at);if(stopped)break;}pending_nested.erase(nested);}}
            }
        }
        if(!stopped&&d.complete!=UINT64_MAX){
            if(!pending.empty()){auto first=pending.begin();for(auto it=pending.begin();it!=pending.end();++it)if(it->second->source.offset<first->second->source.offset)first=it;
                failure(d.complete,first->second->source,"Header Parser V2 constructor target is not a field of this record");}
            else if(!pending_nested.empty()){const operation* first=nullptr;for(const auto& [name,ops]:pending_nested)for(const auto* op:ops)if(!first||op->source.offset<first->source.offset)first=op;failure(d.complete,first->source,"Constructor target is not a field of this record");}
            else if(!waiting.empty()){auto first=waiting.begin();for(auto it=waiting.begin();it!=waiting.end();++it)if(it->second.source.offset<first->second.source.offset)first=it;
                failure(d.complete,first->second.source,"Reference binding names neither a member nor a visible Header static object");}
        }
        auto& normalized=out.normalized;
        for(const auto& [request,cell]:values)if(cell.value.kind!=CK::zero){out.defaults.push_back({request,cell.value,cell.source});normalized.emplace_back(std::string(b.requests[request].member),cell.value);}
        std::sort(normalized.begin(),normalized.end(),[](const auto& x,const auto& y){return x.first<y.first;});
        out.stopped=stopped;for(const auto& [path,cell]:completed_nested)out.nested.push_back({path,cell.value,cell.source});return out;
}
inline std::vector<record_result> evaluate_all(const batch& b,const bound_storage& types,const prepared& p,const ca01::result& canonical,unsigned order){
    std::vector<record_result> outputs(b.declarations.size());std::vector<id> tasks;
    for(id di=0;di<b.declarations.size();++di)if(b.declarations[di].kind==declaration_kind::record)tasks.push_back(di);
    if(order==1)std::reverse(tasks.begin(),tasks.end());
    if(order==2)std::sort(tasks.begin(),tasks.end(),[](id a,id z){return ca01::mix(a)<ca01::mix(z);});
    for(auto di:tasks)outputs[di]=sl01::evaluate_one(b,types,p,canonical,di);
    return outputs;
}
inline void resolve(const batch& b,const ca01::result& canonical,binding_result& result){
    last={};if(!enabled){ei01::resolve(b,canonical,result);last=ei01::last;return;}
    const auto p=ei01::prepare(b,canonical,result.types,last);auto start=ei01::clock::now();auto outputs=sl01::evaluate_all(b,result.types,p,canonical,task_order);last.evaluate=milliseconds(start);
    if(audit_sizes)last.local_bytes=ei01::local_bytes(outputs);start=ei01::clock::now();ei01::commit(outputs,result);last.commit=milliseconds(start);
}
}
