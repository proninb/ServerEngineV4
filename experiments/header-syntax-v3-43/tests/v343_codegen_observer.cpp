#include "mock.hpp"
#include "v3_16_canonical.hpp"
#include "v3_42_canonical.hpp"
#include <cstdint>
template<class T> static std::uint64_t observe(const T& spec) noexcept {
    std::uint64_t sum=spec.base_payload*1009u+spec.length;
    sum+=static_cast<std::uint32_t>(spec.failure);
    for(std::uint32_t i=0;i<spec.length && i<32;++i) {
        sum=sum*33u+static_cast<std::uint32_t>(spec.layers[i].kind);
        sum=sum*33u+spec.layers[i].payload;
    }
    return sum;
}
extern "C" std::uint64_t v343_observe_baseline(const v316::type_spec& s) noexcept {return observe(s);}
extern "C" std::uint64_t v343_observe_lazy(const v342_canonical::type_spec& s) noexcept {return observe(s);}
