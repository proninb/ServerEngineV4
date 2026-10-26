#pragma once
#include <cstdlib>
#include <cstdint>
#include <ostream>
namespace pls10c7 {
inline const bool fast=[](){auto p=std::getenv("PLS10_C7_FAST");return p&&p[0]=='1';}();
inline std::uint64_t fast_calls=0,fallback_calls=0,verified=0;
inline void count(bool used){
#ifdef PLS10_C7_AUDIT
 if(used)++fast_calls;else ++fallback_calls;
#endif
}
inline void print(std::ostream& o){o<<",c7_fast="<<fast<<",c7_fast_calls="<<fast_calls<<",c7_fallback_calls="<<fallback_calls<<",c7_verified="<<verified;}
}
