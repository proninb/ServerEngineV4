#pragma once
// Included after the immutable RG01 tape and friend access definitions.
namespace cw::server::resolved_g01 {
inline endpoint_path_handle access::find_slot(const graph& G,type_ref root,
    std::span<const endpoint_path_step> steps,std::uint64_t hash,std::uint32_t fp,
    std::size_t& empty) noexcept {
    if(G.endpoint_path_index.empty()) return {};
    const auto mask=G.endpoint_path_index.size()-1;
    auto pos=static_cast<std::size_t>(hash)&mask;
    for(std::size_t probe=0;probe<G.endpoint_path_index.size();++probe) {
        const auto& slot=G.endpoint_path_index[pos];
        if(!slot.path) { empty=pos; return {}; }
        if(slot.fingerprint==fp && slot.path.value()<=G.endpoint_paths.size()) {
            const auto& record=G.endpoint_paths[slot.path.value()-1];
            if(record.root_type==root && record.steps.count==steps.size() &&
                record.steps.begin<=G.endpoint_path_step_values.size() &&
                record.steps.count<=G.endpoint_path_step_values.size()-record.steps.begin) {
                const auto existing=std::span<const endpoint_path_step>{G.endpoint_path_step_values}.subspan(record.steps.begin,record.steps.count);
                if(std::equal(existing.begin(),existing.end(),steps.begin(),steps.end())) return slot.path;
            }
        }
        pos=(pos+1)&mask;
    }
    return {};
}
inline bool access::path_prepare(graph& G,const graph& expected,unsigned mode) {
    using namespace path_intern02;
    if(mode&skip_all) {
        copy_vector(G.endpoint_paths,expected.endpoint_paths);
        copy_vector(G.endpoint_path_step_values,expected.endpoint_path_step_values);
        copy_vector(G.endpoint_path_index,expected.endpoint_path_index);
        return true;
    }
    if(mode&reserve_vectors) {
        G.endpoint_paths.reserve(expected.endpoint_paths.capacity());
        G.endpoint_path_step_values.reserve(expected.endpoint_path_step_values.capacity());
    }
    if((mode&reserve_index) && expected.endpoint_paths.size()>G.endpoint_paths.size()) {
        if(!succeeded(G.ensure_endpoint_path_index_capacity(expected.endpoint_paths.size()-G.endpoint_paths.size()))) return false;
        if(G.endpoint_path_index.size()!=expected.endpoint_path_index.size()) return false;
    }
    return true;
}
inline bool access::verify_hashes(const graph& G) {
    if(G.endpoint_path_hashes.size()>G.endpoint_paths.size()) return false;
    for(std::size_t i=0;i<G.endpoint_path_hashes.size();++i) {
        if(G.endpoint_path_hashes[i]==0) continue;
        const auto& record=G.endpoint_paths[i];
        if(G.endpoint_path_hashes[i]!=graph::hash_endpoint_path(record.root_type,
            std::span<const endpoint_path_step>{G.endpoint_path_step_values}.subspan(record.steps.begin,record.steps.count))) return false;
    }
    return true;
}
inline bool access::prepare_path_tape(const graph& expected) {
    auto known=state.baseline->endpoint_paths.size();
    std::size_t existing=0,added=0,steps=0;
    for(auto& o:state.operations) if(o.kind==code::path) {
        if(!o.expected) return false;
        o.flags=o.expected<=known ? 1:0;
        if(o.flags) ++existing;
        else {
            if(o.expected!=known+1) return false;
            ++known; ++added; steps+=o.count;
        }
        o.payload=graph::hash_endpoint_path(o.type,
            std::span<const endpoint_path_step>{state.steps}.subspan(o.begin,o.count));
    }
    std::printf("resolved_g02_paths,existing=%zu,new=%zu,new_steps=%zu,final_path_capacity=%zu,final_step_capacity=%zu,final_index_slots=%zu\n",
        existing,added,steps,expected.endpoint_paths.capacity(),expected.endpoint_path_step_values.capacity(),expected.endpoint_path_index.size());
    return known==expected.endpoint_paths.size();
}
inline bool finish_path02(const graph& expected) noexcept {
    using namespace path_intern02;
    const auto saved_mode=mode;
    struct restore { unsigned old; ~restore(){ mode=old; supplied_hash=0; } } restore_mode{saved_mode};
    if(!access::prepare_path_tape(expected)) return false;
    int pairs=11;
    if(const auto* value=std::getenv("CW_RESOLVED_G_REPEATS")) pairs=std::clamp(std::atoi(value),1,100);
    const std::array<unsigned,9> cases{{prehashed,reserve_vectors,reserve_index,
        reserve_vectors|reserve_index,single_probe,cached_hashes,single_probe|cached_hashes,skip_existing,skip_all}};
    try {
        for(int pair=-2;pair<pairs;++pair) {
            for(unsigned offset=0;offset<cases.size();++offset) {
                const auto candidate=cases[(offset+unsigned(pair+2))%cases.size()];
                for(unsigned arm=0;arm<2;++arm) {
                    const auto selected=((unsigned(pair+2)+offset)%2 ? 1-arm:arm) ? candidate:0;
                    mode=selected; supplied_hash=0;
                    const auto prepare_start=clock::now();
                    auto G=access::clone(*state.baseline);
                    // This oracle intentionally excludes catalogue copying from g_ms.
                    if((selected&skip_all) && !access::path_prepare(*G,expected,selected)) return false;
                    const auto started=clock::now();
                    const bool prepared=(selected&skip_all) || access::path_prepare(*G,expected,selected);
                    const bool success=prepared && (pair<0?execute<true>(*G):execute<false>(*G));
                    const auto stopped=clock::now();
                    const bool parity=success && access::equal(*G,expected) && access::verify_hashes(*G);
                    const auto verified=clock::now();
                    const auto bytes=access::bytes(*G);
                    G.reset();
                    const auto released=clock::now();
                    std::printf("resolved_g02,pair=%d,case=%u,mode=%u,g_ms=%.6f,prepare_ms=%.6f,verify_ms=%.6f,release_ms=%.6f,parity=%d,capacity_bytes=%zu\n",
                        pair,candidate,selected,ms(started,stopped),ms(prepare_start,started),ms(stopped,verified),ms(verified,released),parity,bytes);
                    if(!parity) return false;
                }
            }
        }
        state.baseline.reset();
        return true;
    } catch(...) { std::fprintf(stderr,"resolved_g02 allocation failed\n"); return false; }
}
} // namespace cw::server::resolved_g01
