// V3-43 MSVC /O2 /FAcs probe. The observer is defined in another translation unit.
// This forces an actual escaped type_spec object. It does not prove full-producer codegen.
#include "mock.hpp"
#include "v3_16_canonical.hpp"
#include "v3_42_canonical.hpp"
#include <cstdint>
#if defined(_MSC_VER)
#define V343_NOINLINE __declspec(noinline)
#else
#define V343_NOINLINE __attribute__((noinline))
#endif
extern "C" std::uint64_t v343_observe_baseline(const v316::type_spec&) noexcept;
extern "C" std::uint64_t v343_observe_lazy(const v342_canonical::type_spec&) noexcept;
extern "C" V343_NOINLINE std::uint64_t v343_probe_baseline(std::uint64_t x) {
    v316::type_spec spec;
    spec.base_kind=cw::server::type_ref_kind::intrinsic;
    spec.base_payload=static_cast<std::uint32_t>(x);
    const unsigned count=static_cast<unsigned>(x % 34u);
    for(unsigned i=0;i<count;++i)
        if(!spec.push(i%2?cw::server::derived_type_kind::const_qualified:
                          cw::server::derived_type_kind::pointer, x+i))break;
    return v343_observe_baseline(spec);
}
extern "C" V343_NOINLINE std::uint64_t v343_probe_lazy(std::uint64_t x) {
    v342_canonical::type_spec spec;
    spec.base_kind=cw::server::type_ref_kind::intrinsic;
    spec.base_payload=static_cast<std::uint32_t>(x);
    const unsigned count=static_cast<unsigned>(x % 34u);
    for(unsigned i=0;i<count;++i)
        if(!spec.push(i%2?cw::server::derived_type_kind::const_qualified:
                          cw::server::derived_type_kind::pointer, x+i))break;
    return v343_observe_lazy(spec);
}
