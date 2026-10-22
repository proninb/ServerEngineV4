#pragma once
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>

namespace pd01 {
inline std::ofstream strings, types, semantic, provenance;
inline std::uint64_t sequence=0;
inline unsigned root=0;
inline std::string_view side;
inline bool active=false, failed=false;
inline std::string quote(std::string_view s) {
    std::string out="\"";
    for(char c:s){if(c=='\"')out+='\"';out+=c;}
    return out+'\"';
}
inline void emit(std::ofstream& out,std::string_view kind,std::uint64_t a=0,
    std::uint64_t b=0,std::uint64_t c=0,std::string_view text={}) noexcept {
    if(!active)return;
    try {out<<++sequence<<','<<side<<','<<root<<','<<quote(kind)<<','<<a<<','<<b<<','<<c<<','<<quote(text)<<'\n';if(!out)failed=true;}
    catch(...){failed=true;}
}
template<class F> struct on_exit {F fn;~on_exit()noexcept{try{fn();}catch(...){failed=true;}}};
template<class F> on_exit(F)->on_exit<F>;
inline void open(const std::string& folder){
    auto start=[&](auto& out,const char* name){out.open(folder+"/"+name);if(!out)throw std::runtime_error("cannot open trace");out<<"sequence,side,root,kind,a,b,c,text\n";};
    start(strings,"string-id-events.csv");start(types,"type-id-events.csv");
    start(semantic,"semantic-events.csv");start(provenance,"provenance-events.csv");
}
inline void flush(){for(auto* out:{&strings,&types,&semantic,&provenance}){out->flush();if(!*out)failed=true;}if(failed)throw std::runtime_error("trace write failed");}
}
