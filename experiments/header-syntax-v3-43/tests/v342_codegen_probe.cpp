// To inspect MSVC Release codegen, compile /O2 /std:c++20 /FAcs /c using overlay includes.
// This probe tests initialization only; full benchmark measures end-to-end cost.
#include "mock.hpp"
#include "v3_16_canonical.hpp"
#include "v3_42_canonical.hpp"
#include <cstdint>
#if defined(_MSC_VER)
#define V342_NOINLINE __declspec(noinline)
#else
#define V342_NOINLINE __attribute__((noinline))
#endif
extern "C" V342_NOINLINE std::uint64_t v342_probe_baseline(std::uint64_t x){
    v316::type_spec s;
    s.push(cw::server::derived_type_kind::pointer,x);
    return s.layers[0].payload + s.length;
}
extern "C" V342_NOINLINE std::uint64_t v342_probe_lazy(std::uint64_t x){
    v342_canonical::type_spec s;
    s.push(cw::server::derived_type_kind::pointer,x);
    return s.layers[0].payload + s.length;
}
