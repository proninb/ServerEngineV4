#include "c1_fixtures.inc"

struct policy {
    unsigned calls=0;std::uint32_t slot=0;bool exists=false,accept=true;
    server_status result=server_status::success;
    static server_status run(void* context,std::uint32_t slot,bool exists,bool& accept)noexcept{
        auto& p=*static_cast<policy*>(context);++p.calls;p.slot=slot;p.exists=exists;accept=p.accept;return p.result;
    }
};
void policy_contract(){
    fixture f;policy p;initialization_apply_result result;
    p.accept=false;
    status(f.G.apply_resolved_initialization(f.cx,f.integer,fixture::value(1),policy::run,&p,result));
    require(p.calls==1&&!p.exists&&p.slot==0&&!result.applied&&result.slot==initialization_apply_result::invalid_slot&&f.G.initialization_count()==0,"Reject-new contract");
    p.result=server_status::io_error;
    require(f.G.apply_resolved_initialization(f.cx,f.integer,fixture::value(1),policy::run,&p,result)==server_status::io_error&&f.G.initialization_count()==0,"Failed new decision mutated Graph");
    p.result=server_status::success;p.accept=true;
    status(f.G.apply_resolved_initialization(f.cx,f.integer,fixture::value(1),policy::run,&p,result));
    require(result.slot==0&&!result.existed&&result.applied,"Insert result");
    p.accept=false;
    status(f.G.apply_resolved_initialization(f.cx,f.integer,fixture::value(2),policy::run,&p,result));
    require(result.slot==0&&result.existed&&!result.applied&&p.exists,"Reject-existing contract");f.expect(f.cx,1);
    p.result=server_status::io_error;
    require(f.G.apply_resolved_initialization(f.cx,f.integer,fixture::value(3),policy::run,&p,result)==server_status::io_error,"Callback error not propagated");
    require(result.slot==initialization_apply_result::invalid_slot&&!result.applied,"Error output not cleared");f.expect(f.cx,1);
    p.result=server_status::success;p.accept=true;
    status(f.G.apply_resolved_initialization(f.cx,f.integer,fixture::value(3),policy::run,&p,result));f.expect(f.cx,3);
    require(result.slot==0&&result.existed&&result.applied&&f.G.initialization_count()==1,"Replace result");
    const auto calls=p.calls;
    require(!succeeded(f.G.apply_resolved_initialization(f.cx,f.integer,fixture::value(1),nullptr,&p,result)),"Null policy accepted");
    require(!succeeded(f.G.apply_resolved_initialization({},f.integer,fixture::value(1),policy::run,&p,result)),"Invalid target accepted");
    require(!succeeded(f.G.apply_resolved_initialization(f.cx,f.G.named(f.record),fixture::value(1),policy::run,&p,result)),"Non-scalar type accepted");
    require(p.calls==calls,"Invalid input reached callback");
}
void normalized_cell(){
    fixture f;pls10c2::slot_commit sink(f.G);
    const auto first=sink.apply(f.cx,f.op(1));
    const std::array<endpoint_path_step,1> steps{{{0,endpoint_path_step_kind::member,{}}}};
    endpoint_path_handle path;status(f.G.intern_resolved_endpoint_path(f.G.named(f.record),steps,f.integer,path));
    const object_endpoint wrapped{f.c,endpoint_ref::from_path(path)};
    const auto next=sink.apply(wrapped,f.op(3));const auto late=sink.apply(f.cx,f.op(2));
    require(first.slot==next.slot&&next.slot==late.slot&&sink.cells()==1&&f.G.initialization_count()==1,"Equivalent endpoint encodings obtained two slots");
    require(!late.applied&&late.existed&&sink.decisions==3,"Late operation bypassed decision");f.expect(f.cx,3);
    const auto w=sink.diagnostics();require(w.size()==2&&w[0].current.offset==20&&w[1].current.offset==30,"Normalized warning sequence");
}
void stable_slots_and_seed(){
    fixture f;bool replaced=false;
    status(f.G.add_resolved_initialization(f.bx,f.integer,fixture::value(99),replaced));
    pls10c2::slot_commit sink(f.G);const auto first=sink.apply(f.cx,f.op(1));require(first.slot==1,"Seed slot not accounted for");
    std::vector<std::pair<object_endpoint,std::uint32_t>> saved;
    for(unsigned i=0;i<8192;++i){auto who=f.who(("s"+std::to_string(i)).c_str(),identity_kind::object);object_handle object;
        status(f.G.add_resolved_object(who,f.G.named(f.record),object));const object_endpoint ep{who,f.cx.member};
        const auto inserted=sink.apply(ep,f.op(i+10));saved.push_back({ep,inserted.slot});
    }
    // Reverse revisits cross several vector reallocations and index rehashes.
    for(auto i=saved.rbegin();i!=saved.rend();++i){const auto changed=sink.apply(i->first,f.op(20000));require(changed.slot==i->second&&changed.applied&&changed.existed,"Rehash changed slot");
        require(f.G.initialization_entries()[changed.slot].target==i->first,"Slot no longer addresses target");}
    const auto repeated=sink.apply(f.cx,f.op(2));require(repeated.slot==first.slot,"Early slot unstable");f.expect(f.cx,2);
    // A preexisting Graph value has no Source event in this producer lifetime.
    const auto seed=sink.apply(f.bx,f.op(3));require(seed.slot==0&&seed.existed&&seed.applied,"Seed update");
    require(sink.diagnostics().size()==8193,"Seed generated duplicate warning");
    require(sink.cells()==8194&&f.G.initialization_count()==8194,"Unexpected slot or metadata count");
}
void old_api_parity(){
    fixture legacy,candidate;policy p;initialization_apply_result result;
    for(unsigned n:std::array<unsigned,5>{1,3,2,5,4}){bool replaced=false;
        status(legacy.G.add_resolved_initialization(legacy.cx,legacy.integer,fixture::value(n),replaced));
        status(candidate.G.apply_resolved_initialization(candidate.cx,candidate.integer,fixture::value(n),policy::run,&p,result));
        require(replaced==result.existed&&result.applied,"Legacy replace flag mismatch");}
    const auto a=legacy.G.initialization_entries(),b=candidate.G.initialization_entries();require(a.size()==b.size(),"Legacy count mismatch");
    for(std::size_t i=0;i<a.size();++i)require(a[i].target==b[i].target&&a[i].value==b[i].value,"Always-accept policy differs from old API");
}
int main(){try {
    // Reuse C1 expectations with the metadata map replaced by a dense sidecar.
    interleaving();final_binding();all_arrival_orders();source_map_origins();index_growth();
    policy_contract();normalized_cell();stable_slots_and_seed();old_api_parity();
    std::cout<<"PASS C2: C1 parity, 24 permutations, one normalized slot, 8192 rehash revisits, seed holes, reject/error callbacks, legacy API parity\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
