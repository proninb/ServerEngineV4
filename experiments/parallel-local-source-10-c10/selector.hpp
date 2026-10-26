#pragma once
#include <cstdlib>
#include <ostream>
namespace pls10c10 {
// Private experiment switch, read once; this is buffer lifetime, not a cache.
inline const bool reuse=[] {const auto p=std::getenv("PLS10_C10_REUSE");return !p||*p!='0';}();
inline void print(std::ostream& o){o<<"\npls10c10_reuse="<<reuse;}
}
