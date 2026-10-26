#pragma once
#include <cstdlib>
#include <cstddef>
namespace pls10b {
inline bool enabled(){const auto* p=std::getenv("PLS10_WRITES");return p&&p[0]=='1';}
struct measurements {std::size_t operations=0,direct=0,indirect=0,direct_winners=0,early_losers=0,retained_before=0,retained_after=0,conflict_origins=0;};
inline measurements stats;
}
