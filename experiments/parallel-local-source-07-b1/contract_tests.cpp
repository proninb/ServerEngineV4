#include "generated/aliases.hpp"
#include "owned_reader.hpp"
#include <bit>
#include <iostream>
#include <stdexcept>

using namespace pls07b1;
static void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
static identity_ref who(unsigned n){return identity_ref::from_raw((3u<<30)|n);}
// Synthetic catalog values only; production type IDs come from the type domain.
static type_ref integer(){return std::bit_cast<type_ref>((1u<<30)|static_cast<unsigned>(intrinsic_type::signed_int));}
static type_ref reference_integer(){return std::bit_cast<type_ref>((3u<<30)|1u);}
struct catalog {
    domain_id stamp{1,4};std::vector<object_info> objects;
    domain_id domain()const{return stamp;}
    std::size_t object_count()const{return objects.size();}
    object_info object_at(std::size_t i)const{return objects.at(i);}
    std::optional<object_info> object(identity_ref who)const{for(auto o:objects)if(o.who==who)return o;return {};}
    bool contains_type(type_ref t)const{return t==integer()||t==reference_integer();}
    bool contains_file(file_id f)const{return f==file_id{9}||f==file_id{3}||f==file_id{17};}
};
using adapter=owned_reader<pls07::owned_result,catalog>;
static_assert(CatalogReader<catalog> && FinalStateReader<adapter>);
template<class T>concept HasHistory=requires(T t){t.write_history();};
template<class T>concept HasRawTarget=requires(T t){t.endpoint_ordinal;};
static_assert(!HasHistory<adapter> && !HasRawTarget<value_view>);
struct duplicate_values:adapter {using adapter::adapter;std::size_t value_count()const{return 2;}value_view value_at(std::size_t)const{return adapter::value_at(0);}};
struct duplicate_bindings:adapter {using adapter::adapter;std::size_t binding_count()const{return 2;}binding_view binding_at(std::size_t)const{return adapter::binding_at(0);}};
struct invalid_target:adapter {using adapter::adapter;value_view value_at(std::size_t i)const{auto v=adapter::value_at(i);v.target={99};return v;}};
struct invalid_origin:adapter {using adapter::adapter;value_view value_at(std::size_t i)const{auto v=adapter::value_at(i);v.source.operand.file=file_id{999};return v;}};
struct indirect_cell:adapter {using adapter::adapter;std::array<endpoint_path_step,1> indirect{{{0,endpoint_path_step_kind::dereference,{}}}};
    cell_view cell_at(std::size_t i)const{auto c=adapter::cell_at(i);c.steps=indirect;return c;}};
struct reversed_warnings:adapter {using adapter::adapter;std::size_t warning_count()const{return 2;}
    warning_view warning_at(std::size_t i)const{auto w=adapter::warning_at(0);w.current_event.local_order=i?2:3;return w;}};

int main(){try{
    const std::vector<source_task> tasks{{file_id{9},0},{file_id{3},1}};
    catalog c;c.objects={{who(1),integer(),graph_object_non_default_initializer,construction_origin::explicit_source,construction_value::constant(construction_kind::signed_integer,1)},
                         {who(2),reference_integer(),0,construction_origin::none,{}}};
    pls02::bound_model b;
    const pls02::origin old_at{file_id{9},{1,1}},new_at{file_id{3},{4,1}},macro_at{file_id{17},{2,1}};
    // Intentionally meaningless private transport ordinals: adapter must use
    // canonical result targets and selected provenance, never freed endpoints.
    b.writes.push_back({400,construction_value::constant(construction_kind::signed_integer,1),{0,2},old_at,old_at,false});
    b.writes.push_back({500,construction_value::constant(construction_kind::signed_integer,2),{1,2},new_at,macro_at,false});
    b.bindings.push_back({600,700,{0,3},old_at,old_at});
    b.declaration_origins.push_back({who(1),old_at,old_at,{0,0},true});
    b.declaration_origins.push_back({who(1),new_at,new_at,{1,0},false});
    pls02::result state;state.cells={{who(1),integer(),{}},{who(2),reference_integer(),{}}};
    state.values={{0,1}};state.bindings={{1,0,0}};state.warnings={{old_at,new_at,{0,2},{1,2},0,false}};
    pls07::owned_result owner(std::move(state),std::move(b));b={};
    adapter projection(owner,c,{1,4},tasks);
    require(inspect(projection,c)==issue::incomplete_projection,"Prototype accidentally materialization-ready");
    require(inspect(projection,c,false)==issue::ok,"Projection contract malformed");
    adapter ready(owner,c,{1,4},tasks,readiness::closed_supported_semantics);
    require(inspect(ready,c)==issue::ok,"Valid closed fixture rejected");
    require(ready.value_count()==1&&owner.view().write_count()==2,"Writer consumes history instead of winners");
    require(ready.value_at(0).value.bits()==2&&ready.value_at(0).target.value==0,"Wrong winner/cell");
    require(ready.value_at(0).source.producer_root==file_id{3}&&ready.value_at(0).source.operand.file==file_id{17},"Logical root confused with physical origin");
    require(ready.binding_at(0).target.value==1&&ready.binding_at(0).source.value==0,"Binding direction wrong");
    require(ready.declaration_count()==2&&ready.declaration_at(0).is_extern,"Extern provenance lost");
    require(c.object_at(0).cold_value.bits()==1,"Source initializer classification missing");
    require(inspect(duplicate_values(owner,c,{1,4},tasks,readiness::closed_supported_semantics),c)==issue::duplicate_value,"Writer allowed to choose a new winner");
    require(inspect(duplicate_bindings(owner,c,{1,4},tasks,readiness::closed_supported_semantics),c)==issue::duplicate_binding,"Binding reduction incomplete");
    require(inspect(invalid_target(owner,c,{1,4},tasks,readiness::closed_supported_semantics),c)==issue::invalid_cell,"Foreign cell ordinal accepted");
    require(inspect(invalid_origin(owner,c,{1,4},tasks,readiness::closed_supported_semantics),c)==issue::invalid_provenance,"Unknown physical file accepted");
    require(inspect(indirect_cell(owner,c,{1,4},tasks,readiness::closed_supported_semantics),c)==issue::invalid_path,"Unresolved dereference reached writer");
    require(inspect(reversed_warnings(owner,c,{1,4},tasks,readiness::closed_supported_semantics),c)==issue::invalid_warning_order,"Completion order replaced diagnostic order");
    auto bad_catalog=c;bad_catalog.stamp.snapshot=5;
    require(inspect(ready,bad_catalog)==issue::domain_mismatch,"Stale catalog accepted");
    bad_catalog=c;bad_catalog.objects.pop_back();
    require(inspect(ready,bad_catalog)==issue::missing_object,"Missing object accepted");
    std::vector<source_task> bad_tasks=tasks;bad_tasks[1].source_order=0;
    require(inspect(adapter(owner,c,{1,4},bad_tasks,readiness::closed_supported_semantics),c)==issue::invalid_tasks,"Invalid ranks accepted");
    bad_tasks=tasks;bad_tasks[1].file=bad_tasks[0].file;
    require(!valid_tasks(bad_tasks),"Duplicate Source accepted");
    require(inspect(adapter(owner,c,{},tasks,readiness::closed_supported_semantics),c)==issue::invalid_domain,"Unbound domain accepted");
    replay_request replay{{1,5},replay_policy::reparse_all_remaining_sources_v1,replay_trigger::source_removal,{1,2,3,4,5},tasks};
    require(valid_replay(replay),"Valid replay request rejected");replay.revisions.header=0;
    require(!valid_replay(replay),"Unbound Header context accepted");
    replay.revisions.header=2;replay.remaining_sources={};require(valid_replay(replay),"Removal of all Source must allow empty replay");
    std::cout<<"B1: real owned_result adapter, domain/readiness/provenance/replay contract checks passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
