#pragma once
#include <ostream>
#include <vector>
#include <string>
#include <cstdlib>
namespace pls10c3 {
inline std::size_t workers=0,window=0,peak_ready=0,files_committed=0;
inline std::size_t conflicts=0;
inline std::vector<std::string> trace;
inline void print(std::ostream& o){o<<",ordered_workers="<<workers<<",ordered_window="<<window<<",ordered_peak_ready="<<peak_ready<<",ordered_files="<<files_committed<<",ordered_conflicts="<<conflicts;
 if(std::getenv("PLS10_TRACE"))for(const auto& text:trace)o<<"\nordered_warning="<<text;}
}
