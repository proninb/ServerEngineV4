    void reduce_writes_sparse(){
        pls03::boundary(pls03::write_targets);
        pls10b::stats.operations=model.writes.operation_count();pls10b::stats.retained_before=model.writes.size();
        pls10b::stats.early_losers=model.writes.losers.size();
        // Only current direct winners and the sparse indirect queue remain.
        // Rows retain first-arrival order, preserving legacy canonical numbering
        // and the first failing indirect request without replaying Source syntax.
        pls06::write_heads heads(out.cells.size());
        for(id row=0;row<model.writes.size();++row){auto& op=model.writes[row];current_origin=op.at;current_order=op.event;
            const auto c=op.indirect?canonical(model.endpoints[op.target],false):flat.materialize(op.target,out.cells);
            op.target=c;const auto previous=heads.get(c);
            if(previous==none||model.writes[previous].event<op.event)heads.set(c,row);
        }
        // Producer losers contain direct-entry IDs until all keys materialize.
        for(auto& loser:model.writes.losers)loser.cell=flat.canonical(loser.cell);
        flat.release_keys();pls07::release_endpoints(model);pls03::boundary(pls03::reduction);
        // Retain only final values. Losing values are discarded; only their
        // origin/event is needed for diagnostics across direct/indirect aliases.
        id retained=0;
        for(id row=0;row<model.writes.size();++row){const auto op=model.writes[row];const auto winner=heads.get(op.target);
            if(winner!=row)model.writes.losers.push_back({op.target,op.at,op.event});
            else {model.writes[retained]=op;heads.set(op.target,retained++);}
        }
        model.writes.resize(retained);pls10b::stats.retained_after=retained;
        auto& origins=model.writes.losers;pls10b::stats.conflict_origins=origins.size();
        std::sort(origins.begin(),origins.end(),[](const auto& a,const auto& b){return std::tie(a.cell,a.event)<std::tie(b.cell,b.event);});
        for(std::size_t first=0;first<origins.size();){auto last=first+1;while(last<origins.size()&&origins[last].cell==origins[first].cell)++last;
            for(auto i=first+1;i<last;++i){const auto& p=origins[i-1];const auto& n=origins[i];out.warnings.push_back({p.at,n.at,p.event,n.event,p.cell,false});}
            const auto& p=origins[last-1];const auto& winner=model.writes[heads.get(p.cell)];
            out.warnings.push_back({p.at,winner.at,p.event,winner.event,p.cell,false});first=last;
        }
        std::vector<pls10b::losing_origin>{}.swap(origins);
        heads.each([&](id c,id row){out.values.push_back({c,row});});pls06::stats.populated=out.values.size();
        // Dense heads iterate canonical IDs in order; no final values sort.
        pls03::boundary(pls03::write_cleanup);
    }
