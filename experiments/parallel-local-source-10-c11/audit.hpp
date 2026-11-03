#pragma once
#include <atomic>
#include <iostream>
namespace pls10c9audit {
inline thread_local bool active=false;
inline thread_local unsigned group=0;
inline std::atomic<unsigned long long> group_calls[7]{},group_bytes[7]{};
inline std::atomic<unsigned long long> allocations{0},requested{0},live{0},peak{0};
inline std::atomic<unsigned long long> identity_find{0},identity_resolve{0},object{0},path{0},initialization{0},batch{0},batch_records{0},slot_apply{0},rebind{0},add_link{0},init_lookup{0},link_lookup{0};
struct guard {bool previous=active;unsigned previous_group=group;explicit guard(unsigned g=1){active=true;group=g;}~guard(){active=previous;group=previous_group;}};
inline void count(std::atomic<unsigned long long>& n){if(active)++n;}
inline void print(std::ostream& o){
#define P(name) o<<",c9_audit_" #name "="<<name.load();
P(allocations) P(requested) P(live) P(peak) P(identity_find) P(identity_resolve) P(object) P(path) P(initialization) P(batch) P(batch_records) P(slot_apply) P(rebind) P(add_link) P(init_lookup) P(link_lookup)
for(unsigned g=1;g<7;++g)o<<",c9_audit_group"<<g<<"_calls="<<group_calls[g].load()<<",c9_audit_group"<<g<<"_requested="<<group_bytes[g].load();
#undef P
}
}
