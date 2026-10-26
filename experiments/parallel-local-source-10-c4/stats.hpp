#pragma once
#include "../parallel-local-source-10-c3/stats.hpp"
namespace pls10c4 {
inline std::size_t local_capacity=0;
inline double registration_ms=0,catalog_wall_ms=0,commit_ms=0;
inline void print(std::ostream& out){out<<",project_visibility=1,project_local_capacity="<<local_capacity<<",project_registration_ms="<<registration_ms<<",project_catalog_wall_ms="<<catalog_wall_ms<<",project_commit_ms="<<commit_ms;}
}
