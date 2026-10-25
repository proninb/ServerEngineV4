#pragma once
#include <chrono>
#include <cstdlib>
#include <ostream>
namespace pls08c {
using clock=std::chrono::steady_clock;
inline bool candidate(){const auto* p=std::getenv("PLS08_CANDIDATE");return p&&p[0]=='1';}
inline double planner_ms=0,extraction_ms=0,linking_ms=0,alias_ms=0,writer_ms=0,release_ms=0;
inline double elapsed(clock::time_point t){return std::chrono::duration<double,std::milli>(clock::now()-t).count();}
inline void print(std::ostream& out){out<<",pls08_candidate="<<candidate()<<",pls08_planner_ms="<<planner_ms<<",pls08_extraction_ms="<<extraction_ms
 <<",pls08_linking_ms="<<linking_ms<<",pls08_alias_ms="<<alias_ms<<",pls08_writer_ms="<<writer_ms<<",pls08_release_ms="<<release_ms;}
}
