#pragma once
#include <cstdlib>
#include <ostream>
namespace pls15 {
inline const bool direct=[] {auto p=std::getenv("PLS10_C15_DIRECT");return !p||*p!='0';}();
inline std::size_t declarations=0,removed=0,verified=0;
inline void print(std::ostream& o){o<<"\npls15_direct="<<direct<<",pls15_declarations="<<declarations<<",pls15_removed="<<removed<<",pls15_verified="<<verified;}
}
