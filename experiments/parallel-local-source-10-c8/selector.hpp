#pragma once
#include <cstdint>
#include <cstdlib>
#include <ostream>
namespace pls10c8 {
inline const bool single=[](){auto p=std::getenv("PLS10_C8_SINGLE");return p&&p[0]=='1';}();
inline thread_local bool active=false;
inline std::uint64_t lookups=0,hits=0,misses=0,new_bindings=0,replacements=0,known_absent=0;
struct observation {
 observation(){
#ifdef PLS10_C8_AUDIT
 active=true;
#endif
 }
 ~observation(){active=false;}
};
inline void lookup(bool hit){
#ifdef PLS10_C8_AUDIT
 if(active){++lookups;if(hit)++hits;else ++misses;}
#endif
}
inline void inserted(){
#ifdef PLS10_C8_AUDIT
 if(active){++new_bindings;if(single)++known_absent;}
#endif
}
inline void replaced(){
#ifdef PLS10_C8_AUDIT
 if(active)++replacements;
#endif
}
inline void print(std::ostream& o){o<<",c8_single="<<single<<",c8_lookups="<<lookups<<",c8_hits="<<hits<<",c8_misses="<<misses<<",c8_new_bindings="<<new_bindings<<",c8_replacements="<<replacements<<",c8_known_absent="<<known_absent;}
}
