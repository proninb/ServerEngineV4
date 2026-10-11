// Regression for V3-24 catastrophic per-record reserve() growth.
// Uses the SAME helper compiled into the isolated graph.cpp overlay.
#include "../tools/v324_capacity.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>
struct member {std::uint32_t name, type, access, flags;};
struct construction {std::uint64_t bits; std::uint32_t operand, kind;};
static_assert(sizeof(member)==16 && sizeof(construction)==16);
int main() {
    constexpr std::size_t records=23333, members=708668;
    std::vector<member> fields;
    std::vector<construction> values;
    std::size_t realloc_fields=0, realloc_values=0;
    std::uint64_t requested_bytes=0;
    for (std::size_t rec=0;rec<records;++rec) {
        const std::size_t count=members/records+(rec<members%records?1:0);
        const auto required=fields.size()+count;
        const auto before_fields=fields.capacity(),before_values=values.capacity();
        assert(cw::server::v324_capacity::ensure(fields,required));
        assert(cw::server::v324_capacity::ensure(values,required));
        if (before_fields!=fields.capacity()) {
            ++realloc_fields;
            requested_bytes+=fields.capacity()*sizeof(member);
        }
        if (before_values!=values.capacity()) {
            ++realloc_values;
            requested_bytes+=values.capacity()*sizeof(construction);
        }
        fields.resize(required);
        values.resize(required);
        assert(fields.size()==values.size());
    }
    assert(fields.size()==members);
    assert(realloc_fields<=20 && realloc_values<=20);
    // Should be tens of MB, not the observed 246 GB of V3-24.
    assert(requested_bytes < 150ULL*1024*1024);
    assert(!cw::server::v324_capacity::ensure(
        fields,static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)())+1));
    std::cout << "V324_CAPACITY_PASS records="<<records
              <<" members="<<members
              <<" reallocations="<<(realloc_fields+realloc_values)
              <<" requested_bytes="<<requested_bytes<<'\n';
}
