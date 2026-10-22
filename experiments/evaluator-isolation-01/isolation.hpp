#pragma once
#include "../header-semantic-coverage-03/constructors.hpp"
#include <chrono>
namespace ei01 {
using namespace sb01;
using sc02::action;using sc02::compatible;using sc02::is_reference;using sc02::referent_matches;using sc02::waiting_reference;
inline bool enabled=true;
inline bool audit_sizes=false;
inline unsigned task_order=0; // fixed by the caller before evaluation, never worker state
struct measurements {double actions=0,catalog=0,schemas=0,evaluate=0,commit=0;std::size_t indexed=0,local_bytes=0;};
inline measurements last;
using clock=std::chrono::steady_clock;
inline double milliseconds(clock::time_point t){return std::chrono::duration<double,std::milli>(clock::now()-t).count();}
struct range {id first=none,last=0;};
struct schema {event complete;id definition;std::unordered_map<std::string_view,id> members;bool indexed=false;};
struct prepared {
    std::vector<std::vector<action>> actions;
    std::vector<bool> gaps,needs_fields;
    std::vector<range> fields;
    std::unordered_map<id,schema> schemas;
};
inline prepared prepare(const batch& b,const ca01::result& canonical,const bound_storage& types,measurements& timing){
    prepared p;auto start=clock::now();p.actions.resize(b.declarations.size());p.gaps.resize(b.declarations.size());p.needs_fields.resize(b.declarations.size());
    for(const auto& op:b.operations){if(op.kind==operation_kind::gap)p.gaps[op.definition]=true;if(op.kind==operation_kind::nested||op.kind==operation_kind::scalar||op.kind==operation_kind::reference)p.needs_fields[op.definition]=true;p.actions[op.definition].push_back({op.at,none,&op});}
    b.requests.range(0,b.requests.size(),[&](auto i,const request& r){if(!r.base&&p.needs_fields[r.definition]&&!p.gaps[r.definition])p.actions[r.definition].push_back({r.at+1,static_cast<id>(i),nullptr});});
    for(auto& events:p.actions)std::sort(events.begin(),events.end(),[](auto a,auto z){return a.at<z.at;});timing.actions=milliseconds(start);start=clock::now();
    p.fields.resize(b.declarations.size());
    for(id di=0;di<b.declarations.size();++di){const auto& d=b.declarations[di];if(d.kind!=declaration_kind::record||d.complete==UINT64_MAX)continue;const auto rep=canonical.representative[d.symbol];auto it=p.schemas.find(rep);if(it==p.schemas.end()||d.complete<it->second.complete)p.schemas.insert_or_assign(rep,schema{d.complete,di});}
    b.requests.range(0,b.requests.size(),[&](auto i,const request& r){if(r.base)return;auto& fields=p.fields[r.definition];fields.first=(std::min)(fields.first,static_cast<id>(i));fields.last=static_cast<id>(i+1);});timing.catalog=milliseconds(start);start=clock::now();
    // Discover only schemas reached by complete nested paths. This pass emits no
    // diagnostics: visibility and failure order belong to record evaluation.
    for(const auto& op:b.operations){if(op.kind!=operation_kind::nested)continue;id root=none;const auto fields=p.fields[op.definition];
        for(id i=fields.first;i<fields.last;++i){const auto& r=b.requests[i];if(!r.base&&r.definition==op.definition&&r.member==op.target){root=i;break;}}
        if(root==none)continue;auto type=types[root];
        for(id step_id=0;step_id<op.path.count;++step_id){const auto& step=op.path.arena->steps[op.path.offset+step_id];
            if(!step.name.empty()){
                if(type.entity==none||!type.layers.empty())break;auto found=p.schemas.find(type.entity);if(found==p.schemas.end())break;auto& entry=found->second;
                if(!entry.indexed){const auto fields=p.fields[entry.definition];for(id i=fields.first;i<fields.last;++i){const auto& r=b.requests[i];if(!r.base&&r.definition==entry.definition)entry.members.emplace(r.member,i);}entry.indexed=true;++timing.indexed;}
                const auto field=entry.members.find(step.name);if(field==entry.members.end())break;type=types[field->second];
            }else{if(type.layers.empty()||type.layers.back().kind!=D::bounded_array||step.index>=type.layers.back().payload)break;type.layers=type.layers.first(type.layers.size()-1);}
        }
    }timing.schemas=milliseconds(start);return p;
}
struct provenance {event assignment=0,decision=0;origin source;};
struct value_cell {CV value;provenance source;};
struct local_default {id request;CV value;provenance source;};
struct local_nested {std::string path;CV value;provenance source;};
struct counters {std::size_t reference_events=0,skipped_reference_events=0,immediate_references=0,queued_references=0,resolved_references=0,stale_references=0,pending_reference_targets=0,superseded_reference_targets=0,constructors=0,scalar_operations=0,covered_definitions=0,gap_definitions=0;};
struct record_result {
    id definition=none,owner=none;event begin=0,complete=UINT64_MAX;origin source,complete_source;
    issue first;bool stopped=false; counters count;
    std::vector<local_default> defaults;std::vector<local_nested> nested;
    std::vector<std::pair<std::string,CV>> normalized;
};
inline record_result evaluate_one(const batch& b,const bound_storage& types,const prepared& p,const ca01::result& canonical,id di){
    record_result out;const auto& d=b.declarations[di];out.definition=di;out.owner=canonical.representative[d.symbol];out.begin=d.begin;out.complete=d.complete;out.source=d.source;out.complete_source=d.complete_source;
    const auto& events=p.actions[di];
    if(p.gaps[di]){++out.count.gap_definitions;for(const auto& e:events)if(e.op&&e.op->kind==operation_kind::reference)++out.count.skipped_reference_events;return out;}
    if(d.complete!=UINT64_MAX)++out.count.covered_definitions;
        std::unordered_map<std::string_view,id> fields;
        std::vector<id> members;
        std::unordered_map<std::string_view,const operation*> pending;
        std::unordered_map<id,value_cell> values;
        std::unordered_map<id,std::uint64_t> generations;
        struct local_waiting {id target;std::uint64_t generation;origin source;event assignment;};
        std::unordered_multimap<std::string_view,local_waiting> waiting;
        std::unordered_map<std::string_view,std::vector<const operation*>> pending_nested;
        std::map<std::string,value_cell> completed_nested;
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
    for(auto di:tasks)outputs[di]=evaluate_one(b,types,p,canonical,di);
    return outputs;
}
inline void commit(std::span<const record_result> outputs,binding_result& result){
    std::map<std::pair<id,std::string>,CV> committed_nested;
    std::unordered_map<id,std::vector<std::pair<std::string,CV>>> definitions;
    for(const auto& out:outputs){if(out.definition==none)continue;earliest(result.first,out.first);
        result.reference_events+=out.count.reference_events;
        result.skipped_reference_events+=out.count.skipped_reference_events;
        result.immediate_references+=out.count.immediate_references;
        result.queued_references+=out.count.queued_references;
        result.resolved_references+=out.count.resolved_references;
        result.stale_references+=out.count.stale_references;
        result.pending_reference_targets+=out.count.pending_reference_targets;
        result.superseded_reference_targets+=out.count.superseded_reference_targets;
        result.constructors+=out.count.constructors;
        result.scalar_operations+=out.count.scalar_operations;
        result.covered_definitions+=out.count.covered_definitions;
        result.gap_definitions+=out.count.gap_definitions;
        for(const auto& value:out.defaults)result.defaults.push_back({value.request,value.value});
        if(out.stopped||out.complete==UINT64_MAX||out.count.gap_definitions)continue;
        auto [definition,inserted]=definitions.try_emplace(out.owner,out.normalized);
        if(!inserted&&definition->second!=out.normalized){earliest(result.first,{error_kind::coverage,out.complete,out.complete_source,2,"Header Parser V2 record definition conflicts with G"});continue;}
        for(const auto& value:out.nested){auto [it,added]=committed_nested.emplace(std::make_pair(out.owner,value.path),value.value);if(!added&&it->second!=value.value){earliest(result.first,{error_kind::coverage,out.complete,out.complete_source,2,"Nested constructor default conflicts with Graph"});break;}}
    }
    for(const auto& [key,value]:committed_nested)result.nested.push_back({key.first,key.second,value});
    std::sort(result.defaults.begin(),result.defaults.end(),[](auto a,auto z){return a.request<z.request;});
}
inline std::size_t local_bytes(std::span<const record_result> outputs){std::size_t n=outputs.size()*sizeof(record_result);for(const auto& out:outputs){n+=out.defaults.capacity()*sizeof(local_default)+out.nested.capacity()*sizeof(local_nested)+out.normalized.capacity()*sizeof(out.normalized[0]);for(const auto& value:out.normalized)if(value.first.capacity()>15)n+=value.first.capacity()+1;for(const auto& value:out.nested)if(value.path.capacity()>15)n+=value.path.capacity()+1;}return n;}
inline void resolve(const batch& b,const ca01::result& canonical,binding_result& result){
    last={};if(!enabled){sc02::resolve(b,canonical,result);return;}
    const auto p=prepare(b,canonical,result.types,last);auto start=clock::now();auto outputs=evaluate_all(b,result.types,p,canonical,task_order);last.evaluate=milliseconds(start);
    if(audit_sizes)last.local_bytes=local_bytes(outputs);start=clock::now();commit(outputs,result);last.commit=milliseconds(start);
}
}
