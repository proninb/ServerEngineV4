#pragma once
// Separate diagnostic target: these inclusive clocks are not speed evidence.
#ifdef CW_SOURCE_INDEX_AUDIT
#include <array>
#include <chrono>
#include <cstdint>
#include <ostream>
namespace cw::server::source_index_audit {
enum operation { visible, identity_find, where_object, where_type, member_find, endpoint_find, strings_lookup, string_hash, identity_rehash, strings_rehash, graph_rehash, operation_count };
inline constexpr const char* names[]={"visible","identity_find","where_object","where_type","member_find","endpoint_find","strings_lookup","string_hash","identity_rehash","strings_rehash","graph_rehash"};
enum counter { visible_requests, cacheable_requests, repeated_positive, repeated_negative, reused, scope_steps, identity_probes, string_probes, counter_count };
inline constexpr const char* counter_names[]={"visible_requests","cacheable_requests","repeated_positive","repeated_negative","reused","scope_steps","identity_probes","string_probes"};
struct state { bool enabled=false, active=false; std::array<std::uint64_t,operation_count> calls{}, ns{}; std::array<std::uint64_t,counter_count> counts{}; };
inline thread_local state data;
struct root { bool previous; root() noexcept:previous(data.active) {data.active=data.enabled;} ~root(){data.active=previous;} };
struct timer {
    operation op; bool active; std::chrono::steady_clock::time_point start;
    explicit timer(operation value) noexcept:op(value),active(data.active) {if(active){++data.calls[op]; start=std::chrono::steady_clock::now();}}
    ~timer(){if(active)data.ns[op]+=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());}
};
inline void print(std::ostream& out) {
    for(unsigned i=0;i<operation_count;++i)out<<",index_audit_"<<names[i]<<"_calls="<<data.calls[i]<<",index_audit_"<<names[i]<<"_inclusive_ms="<<data.ns[i]/1e6;
    for(unsigned i=0;i<counter_count;++i)out<<",index_audit_"<<counter_names[i]<<"="<<data.counts[i];
}
}
#define CW_INDEX_AUDIT_JOIN_(a,b) a##b
#define CW_INDEX_AUDIT_JOIN(a,b) CW_INDEX_AUDIT_JOIN_(a,b)
#define CW_INDEX_TIMER(op) ::cw::server::source_index_audit::timer CW_INDEX_AUDIT_JOIN(index_timer_,__LINE__){::cw::server::source_index_audit::op}
#define CW_INDEX_COUNT(c) do { if(::cw::server::source_index_audit::data.active) ++::cw::server::source_index_audit::data.counts[::cw::server::source_index_audit::c]; } while(false)
#define CW_INDEX_ROOT() ::cw::server::source_index_audit::root index_root_guard
#else
#define CW_INDEX_TIMER(op) ((void)0)
#define CW_INDEX_COUNT(c) ((void)0)
#define CW_INDEX_ROOT() ((void)0)
#endif
