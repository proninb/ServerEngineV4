#include "direct_commit.hpp"
#include <array>
#include <iostream>
#include <numeric>

using namespace pls10c1;
struct fixture {
    string_table strings;identity_space ids{strings};graph G;
    type_ref integer;type_handle record,holder;identity_ref b,c,a;
    object_endpoint bx,cx,ref,broot,croot;
    string_id text(const char* s){string_id n;status(strings.intern(s,n));return n;}
    identity_ref who(const char* s,identity_kind kind){identity_ref n;status(ids.resolve(ids.root(),text(s),kind,n));return n;}
    fixture(){
        integer=G.intrinsic(intrinsic_type::signed_int);
        status(G.declare_record(who("I",identity_kind::type),graph_record_kind::struct_type,record));
        const std::array<member_record,1> fields{{{text("x"),integer,graph_member_access::public_access}}};
        const std::array<construction_value,1> defaults{{value(7)}};
        status(G.define_record(record,graph_record_kind::struct_type,fields,defaults));
        type_ref reference;status(G.derive(G.named(record),derived_type_kind::lvalue_reference,0,reference));
        status(G.declare_record(who("R",identity_kind::type),graph_record_kind::struct_type,holder));
        const std::array<member_record,1> references{{{text("ref"),reference,graph_member_access::public_access}}};
        status(G.define_record(holder,graph_record_kind::struct_type,references));
        b=who("b",identity_kind::object);c=who("c",identity_kind::object);a=who("a",identity_kind::object);
        object_handle object;status(G.add_resolved_object(b,G.named(record),object));status(G.add_resolved_object(c,G.named(record),object));status(G.add_resolved_object(a,G.named(holder),object));
        bx={b,G.find_member(record,text("x"))};cx={c,G.find_member(record,text("x"))};ref={a,G.find_member(holder,text("ref"))};
        endpoint_path_handle path;status(G.intern_resolved_endpoint_path(G.named(record),{},G.named(record),path));
        broot={b,endpoint_ref::from_path(path)};croot={c,endpoint_ref::from_path(path)};
    }
    static construction_value value(std::uint64_t v){return construction_value::constant(construction_kind::signed_integer,v);}
    operation op(unsigned n,unsigned source=0){return {value(n),integer,{{source,n},file_id{source+1},n*10,1}};}
    void expect(object_endpoint e,unsigned n){object_initialization_record r;require(G.initialization(e,r)&&r.value==value(n),"Incorrect final value");}
    void bind(object_endpoint target){link_handle link;bool replaced;status(G.rebind_resolved_link(target,ref,link,replaced));}
    object_endpoint final_member()const{
        // Small-fixture oracle only, not a production alias resolver.
        for(const auto& l:G.link_entries())if(l.target==ref){
            if(l.source==broot)return bx;if(l.source==croot)return cx;
        }
        throw std::runtime_error("Unbound reference");
    }
};

void replacement(){
    fixture f;bool replaced=false;
    status(f.G.add_resolved_initialization(f.cx,f.integer,fixture::value(1),replaced));require(!replaced,"First write replaced");
    const auto count=f.G.initialization_count();
    status(f.G.add_resolved_initialization(f.cx,f.integer,fixture::value(3),replaced));
    require(replaced&&f.G.initialization_count()==count,"Replacement changed slot count");f.expect(f.cx,3);
    require(f.G.initialization_entries()[0].target==f.cx,"Replacement moved slot");
    // Raw API deliberately has no order guard: late call with old event wins.
    status(f.G.add_resolved_initialization(f.cx,f.integer,fixture::value(2),replaced));f.expect(f.cx,2);
}
void endpoint_encoding(){
    fixture f;bool replaced=false;
    status(f.G.add_resolved_initialization(f.cx,f.integer,fixture::value(1),replaced));
    const std::array<endpoint_path_step,1> path{{{0,endpoint_path_step_kind::member,{}}}};
    endpoint_path_handle handle;status(f.G.intern_resolved_endpoint_path(f.G.named(f.record),path,f.integer,handle));
    const object_endpoint wrapped{f.c,endpoint_ref::from_path(handle)};
    require(wrapped!=f.cx,"Fixture needs distinct raw encodings");
    status(f.G.add_resolved_initialization(wrapped,f.integer,fixture::value(2),replaced));
    require(!replaced&&f.G.initialization_count()==2,"Graph endpoint key behavior changed");
    // Both describe x; direct commit must receive ONE normalized endpoint.
    // A path wrapper is not automatically folded into direct-member encoding.
}
void interleaving(){
    fixture f;direct_commit sink(f.G);
    sink.apply(f.cx,f.op(1));std::vector<pending_operation> pending{{0,f.op(2)}};sink.apply(f.cx,f.op(3));
    f.bind(f.croot);finish(pending,sink,[&](auto){return f.final_member();});f.expect(f.cx,3);
    require(pending.empty()&&sink.graph_calls==2&&sink.skipped_older==1,"Older deferred write reached G");
    const auto warnings=sink.diagnostics();require(warnings.size()==2,"Missing warning transition");
    require(warnings[0].previous.offset==10&&warnings[0].current.offset==20&&warnings[1].previous.offset==20&&warnings[1].current.offset==30,"Warnings must be 1 -> 2 -> 3");
    require(sink.cells()==1&&sink.losing_events()==2,"Unexpected metadata cardinality");
    // A type default is not an earlier Source occurrence.
    const auto* original=f.G.construction(f.record,f.G.find_member(f.record,f.text("x")));
    require(original&&*original==fixture::value(7),"Source mutated Header default");
}
void final_binding(){
    fixture f;direct_commit sink(f.G);f.bind(f.broot);
    std::vector<pending_operation> pending{{0,f.op(10)}};
    f.bind(f.croot);finish(pending,sink,[&](auto){return f.final_member();});f.expect(f.cx,10);
    object_initialization_record none;require(!f.G.initialization(f.bx,none),"Applied against transient binding");
    require(sink.graph_calls==1&&sink.diagnostics().empty(),"First Source write warned");
}
void all_arrival_orders(){
    std::array<unsigned,4> arrival{1,2,3,4};unsigned cases=0;
    do{fixture f;direct_commit sink(f.G);for(auto n:arrival)sink.apply(f.cx,f.op(n));f.expect(f.cx,4);
        const auto w=sink.diagnostics();require(w.size()==3,"Wrong warning count");
        for(unsigned i=0;i<3;++i)require(w[i].previous.event.operation==i+1&&w[i].current.event.operation==i+2,"Arrival affected diagnostics");++cases;
    }while(std::next_permutation(arrival.begin(),arrival.end()));require(cases==24,"Permutation coverage");
}
void source_map_origins(){
    fixture f;direct_commit sink(f.G);
    sink.apply(f.cx,f.op(1,0));sink.apply(f.cx,f.op(3,2));sink.apply(f.cx,f.op(2,1));f.expect(f.cx,3);
    cw::server::source_map map;status(map.reset(3));
    sink.provenance_to(map,{file_id{1},file_id{2},file_id{3}});status(map.finalize(3,f.ids,f.G));
    for(unsigned i=1;i<=3;++i){const auto root=map.root(file_id{i});require(root.size()==1&&root[0].file==file_id{i}&&root[0].data==source_data_ref::object(f.c),"Losing contributor missing from SourceMap");}
    const auto w=sink.diagnostics();require(w[0].previous.file==file_id{1}&&w[0].current.file==file_id{2}&&w[1].current.file==file_id{3},"Origins lost on replacement");
    // Same-root repetitions are intentionally deduplicated by native SourceMap.
    cw::server::source_map dedup;status(dedup.reset(1));status(dedup.begin_root(file_id{1}));
    status(dedup.add(file_id{1},source_data_ref::object(f.c)));status(dedup.add(file_id{1},source_data_ref::object(f.c)));status(dedup.end_root());
    status(dedup.finalize(1,f.ids,f.G));
    require(dedup.root(file_id{1}).size()==1,"SourceMap must deduplicate contributions");
}
void index_growth(){
    fixture f;direct_commit sink(f.G);const auto original=f.op(1);sink.apply(f.cx,original);
    for(unsigned i=0;i<4096;++i){object_handle object;auto who=f.who(("o"+std::to_string(i)).c_str(),identity_kind::object);
        status(f.G.add_resolved_object(who,f.G.named(f.record),object));sink.apply({who,f.cx.member},f.op(i+10));}
    sink.apply(f.cx,f.op(2));f.expect(f.cx,2);require(sink.cells()==4097&&f.G.initialization_count()==4097,"Growth lost metadata or changed slot ownership");
    require(sink.diagnostics().size()==1,"Unrelated cells conflicted");
}
int main(){try{
    replacement();endpoint_encoding();interleaving();final_binding();all_arrival_orders();source_map_origins();index_growth();
    std::cout<<"PASS: replacement; raw API has no Source order; endpoint normalization required; 1->2->3; final binding; 24 arrival permutations; SourceMap origins; index growth\n";
    std::cout<<"C1 scope: typed-input feasibility, not integrated PUBLISH or performance evidence\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
