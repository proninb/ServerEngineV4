#pragma once
// Benchmark-only exclusive wall clocks. Production builds compile all probes out.
#ifdef CW_PUBLISH_SOURCE_PROFILE
#include <array>
#include <chrono>
#include <cstdint>
#include <ostream>
namespace cw::server::source_profile {
enum category { other, tokens, strings, symbols_types, endpoints, objects, links, initializations, provenance, indexes, type_validation, count };
inline constexpr const char* names[] = {"other", "tokens", "strings", "symbols_types", "endpoints", "objects", "links", "initializations", "provenance", "indexes", "type_validation"};
struct state {
    bool enabled=false, active=false;
    unsigned current=other;
    std::uint64_t roots=0, sampled_roots=0, stamp=0, source_ns=0;
    std::uint32_t seed=0;
    std::array<std::uint64_t,count> ns{}, calls{}, charges{};
    double clock_charge_ns=0;
    std::uint64_t (*clock)()=nullptr;
};
inline thread_local state data;
inline std::uint64_t now() noexcept {
    return data.clock ? data.clock() : static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
inline void charge(std::uint64_t t) noexcept { data.ns[data.current]+=t-data.stamp; ++data.charges[data.current]; data.stamp=t; }
struct scope {
    bool entered=false; unsigned previous=other;
    explicit scope(category c) noexcept {
        if (!data.active || data.current==static_cast<unsigned>(c)) return;
        entered=true; previous=data.current; charge(now()); data.current=c; ++data.calls[c];
    }
    ~scope() { if(entered) { charge(now()); data.current=previous; } }
};
struct root {
    bool entered=false; std::uint64_t start=0;
    explicit root(std::uint32_t id) noexcept {
        if(!data.enabled) return;
        entered=true; start=now(); ++data.roots;
        // Mix file identity to avoid periodic aliases in repeated generated input.
        std::uint32_t h=id^data.seed; h^=h>>16; h*=0x7feb352du; h^=h>>15; h*=0x846ca68bu; h^=h>>16;
        data.active=(h&63u)==0; data.current=other; data.stamp=start;
        if(data.active) ++data.sampled_roots;
    }
    ~root() { if(entered) { const auto t=now(); if(data.active) charge(t); data.source_ns+=t-start; data.active=false; } }
};
// Empirical empty alternating scopes include clock, TLS accounting and guard costs.
// This is an overhead estimate, not a correction for cache effects or scheduling.
inline void calibrate() noexcept {
    const auto saved=data;
    double minimum=1e30;
    for(unsigned repeat=0;repeat<7;++repeat) {
        data={}; data.active=true; data.stamp=now();
        for(unsigned i=0;i<100000;++i) { scope a{tokens}; }
        charge(now());
        std::uint64_t ns=0, charges=0;
        for(unsigned i=0;i<count;++i) {ns+=data.ns[i]; charges+=data.charges[i];}
        const double cost=static_cast<double>(ns)/static_cast<double>(charges);
        if(cost<minimum) minimum=cost;
    }
    data=saved; data.clock_charge_ns=minimum;
}
inline void print(std::ostream& out) {
    std::uint64_t sum=0; for(auto n:data.ns) sum+=n;
    out<<",source_profile_roots="<<data.roots<<",source_profile_sampled_roots="<<data.sampled_roots
       <<",source_profile_seed="<<data.seed<<",source_profile_clock_charge_ns="<<data.clock_charge_ns
       <<",source_profile_wall_ms="<<data.source_ns/1e6<<",source_profile_sampled_ms="<<sum/1e6;
    for(unsigned i=0;i<count;++i) out<<",source_sample_"<<names[i]<<"_ms="<<data.ns[i]/1e6
        <<",source_sample_"<<names[i]<<"_calls="<<data.calls[i]
        <<",source_sample_"<<names[i]<<"_adjusted_estimate_ms="
        <<(static_cast<double>(data.ns[i])>data.clock_charge_ns*data.charges[i]
            ? (static_cast<double>(data.ns[i])-data.clock_charge_ns*data.charges[i])/1e6 : 0.0);
}
}
#define CW_SOURCE_PROFILE_JOIN_(a,b) a##b
#define CW_SOURCE_PROFILE_JOIN(a,b) CW_SOURCE_PROFILE_JOIN_(a,b)
#define CW_SOURCE_PROFILE(c) ::cw::server::source_profile::scope CW_SOURCE_PROFILE_JOIN(source_probe_,__LINE__){::cw::server::source_profile::c}
#define CW_SOURCE_PROFILE_ROOT(id) ::cw::server::source_profile::root source_root_probe{id}
#else
#define CW_SOURCE_PROFILE(c) ((void)0)
#define CW_SOURCE_PROFILE_ROOT(id) ((void)0)
#endif
