#pragma once
#include <atomic>
#include <cstdint>
#include <ostream>
namespace cw::server::member_index_audit {
inline std::atomic<std::uint64_t> ensure_calls=0,growths=0,scanned_slots=0,
    rebuilt_entries=0,direct_entries=0,insert_probes=0,rehash_probes=0,
    maximum_probe=0,allocated_slots=0,final_capacity=0;
inline thread_local bool rebuilding=false;
struct rebuild_scope {
    bool previous=rebuilding;
    rebuild_scope(){rebuilding=true;}
    ~rebuild_scope(){rebuilding=previous;}
};
inline void insert(std::uint64_t probes){
    (rebuilding?rebuilt_entries:direct_entries).fetch_add(1,std::memory_order_relaxed);
    (rebuilding?rehash_probes:insert_probes).fetch_add(probes,std::memory_order_relaxed);
    auto old=maximum_probe.load(std::memory_order_relaxed);
    while(old<probes&&!maximum_probe.compare_exchange_weak(old,probes,std::memory_order_relaxed)){}
}
inline void print(std::ostream& out){
    out<<",member_idx_ensure_calls="<<ensure_calls.load()<<",member_idx_growths="<<growths.load()
        <<",member_idx_scanned_slots="<<scanned_slots.load()<<",member_idx_rebuilt_entries="<<rebuilt_entries.load()
        <<",member_idx_direct_entries="<<direct_entries.load()<<",member_idx_insert_probes="<<insert_probes.load()
        <<",member_idx_rehash_probes="<<rehash_probes.load()<<",member_idx_max_probe="<<maximum_probe.load()
        <<",member_idx_allocated_slots="<<allocated_slots.load()<<",member_idx_final_capacity="<<final_capacity.load();
}
}
