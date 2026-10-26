    void link_bindings_final(){
        pls10::stats={};
        pls03::boundary(pls03::header);
        for(const auto& link:G.link_entries()){
            const auto target=import_endpoint(link.target),source=import_endpoint(link.source);
            if(has_dereference(model.endpoints[target].steps))fail("Indirect Header binding target outside first slice");
            header_bindings[canonical(model.endpoints[target],false)]=source;
        }
        pls03::boundary(pls03::catalog);
        std::vector<id> indirect;
        const auto later=[&](id a,id b){return model.bindings[b].event<model.bindings[a].event;};
        for(id b=0;b<model.bindings.size();++b){const auto& op=model.bindings[b];current_origin=op.at;current_order=op.event;
            if(has_dereference(model.endpoints[op.target].steps))indirect.push_back(b);
            else final_catalog.select_direct(canonical(model.endpoints[op.target],false),b,later);
        }
        ++out.link_rounds;
        pls10::stats.direct_operations=model.bindings.size()-indirect.size();pls10::stats.indirect_operations=indirect.size();
        // No round is needed for direct reference targets. Nested targets can
        // depend on the previous final-binding context; preserve the established
        // seed/context/error policy with sparse deltas, not copies of direct.
        if(!indirect.empty()){
            bool done=false;
            for(std::size_t round=0;round<=indirect.size()+1;++round){
                final_catalog.begin_round();clear_reference_state();std::optional<failure> unresolved;
                for(auto b:indirect){const auto& op=model.bindings[b];current_origin=op.at;current_order=op.event;
                    try{final_catalog.select_next(canonical(model.endpoints[op.target],false),b,later);}
                    catch(const failure& f){if(!unresolved)unresolved=f;clear_reference_state();}}
                ++out.link_rounds;++pls10::stats.rounds;pls10::stats.target_evaluations+=indirect.size();
                const bool stable=final_catalog.commit_round();
                if(stable){if(unresolved)throw *unresolved;done=true;break;}
            }
            if(!done)fail("Nested reference target linking did not converge");
        }
        clear_reference_state();pls03::boundary(pls03::references);
        const auto keys=final_catalog.keys();pls10::stats.final_bindings=keys.size();
        for(auto c:keys){const auto b=final_catalog.find(c);current_origin=model.bindings[b].at;current_order=model.bindings[b].event;
            out.bindings.push_back({c,follow(c,0),b});}
        pls10::stats.capacity_bytes=final_catalog.bytes();
        // A2: for direct targets, N unique final targets for N operations proves
        // no duplicates. No diagnostic records or second target pass is needed.
        // Nested targets retain the original diagnostic projection in this slice.
        if constexpr(SparseDiagnostics){if(indirect.empty()&&keys.size()==model.bindings.size()){
            pls10::stats.diagnostic_pass_skipped=true;return;}}
        binding_diagnostics();
    }
