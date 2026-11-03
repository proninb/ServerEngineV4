#include "resolver.hpp"
#include <array>
#include <bit>
#include <iostream>
#include <stdexcept>
using namespace cw::server;
void check(bool ok) { if (!ok) throw std::runtime_error("cache audit mismatch"); }
int main() {
    // Synthetic valid bit patterns: no Graph lookup or semantic resolution.
    // Test the actual generated cache methods, not a duplicate implementation.
    graph G;
    const auto name = std::bit_cast<string_id>(std::uint32_t{1});
    const auto member = std::bit_cast<member_index>(std::uint32_t{0});
    const auto type = std::bit_cast<type_ref>(std::uint32_t{0x40000001});
    std::array<std::uint32_t, 65536> seen{};
    type_handle a, b;
    for (std::uint32_t n=1; n<=65537; ++n) {
        auto record=std::bit_cast<type_handle>(n);
        auto slot=arena_test_resolver::transition_slot(arena_test_resolver::transition_key(record,name));
        if (seen[slot]) { a=std::bit_cast<type_handle>(seen[slot]); b=record; break; }
        seen[slot]=n;
    }
    check(bool(a) && bool(b));
    const std::array<endpoint_path_step,1> bases{{{0,endpoint_path_step_kind::base,{}}}};
    for (bool churn : {false,true}) {
        arena_test_resolver resolver{G};
        for (unsigned i=0;i<100000;++i) {
            auto record=churn && (i%2) ? b : a;
            member_index m; type_handle owner; type_ref value;
            std::vector<endpoint_path_step> path;
            if (!resolver.cached_transition(record,name,m,owner,value,path)) {
                resolver.remember_transition(record,name,member,a,type,bases);
                check(resolver.cached_transition(record,name,m,owner,value,path));
            }
            check(m==member && owner==a && value==type && path.size()==1);
        }
        auto s=resolver.inheritance_arena_audit();
        check(s.live_slots==1 && s.live_base_steps==1);
        check(s.stored_base_steps-s.live_base_steps==s.retired_base_steps);
        check(s.stored_base_steps==(churn?100000u:1u));
        std::cout << (churn?"churn":"reuse") << " requests="<<s.requests<<" hits="<<s.hits
            <<" overwrites="<<s.overwritten_slots<<" retired="<<s.retired_base_steps
            <<" live="<<s.live_base_steps<<" stored="<<s.stored_base_steps
            <<" capacity="<<s.capacity_base_steps<<'\n';
    }
}
