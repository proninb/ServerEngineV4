#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <vector>

namespace pls09b1 {
inline bool enabled(){const auto* value=std::getenv("PLS09_INDEXED");return value&&value[0]=='1';}
struct empty_state {};

// Cache/visiting belong to a binding context, not to global WHO identity.
// No pointer/reference into entries is exposed or held across recursive calls.
class reference_state {
    struct entry {
        std::uint32_t resolved=UINT32_MAX;
        std::uint32_t cached_generation=0;
        std::uint32_t visiting_generation=0;
    };
    std::vector<entry> entries;
    std::uint32_t generation;
    void ensure(std::uint32_t cell){if(cell>=entries.size())entries.resize(static_cast<std::size_t>(cell)+1);}
public:
    // Initial generation is injectable solely to test rollover without 2^32 clears.
    explicit reference_state(std::uint32_t initial_generation=1):generation(initial_generation?initial_generation:1){}
    bool find(std::uint32_t cell,std::uint32_t& resolved)const{
        if(cell>=entries.size()||entries[cell].cached_generation!=generation)return false;
        resolved=entries[cell].resolved;return true;
    }
    bool enter(std::uint32_t cell){
        ensure(cell);if(entries[cell].visiting_generation==generation)return false;
        entries[cell].visiting_generation=generation;return true;
    }
    void leave(std::uint32_t cell){if(cell<entries.size())entries[cell].visiting_generation=0;}
    void store(std::uint32_t cell,std::uint32_t resolved){
        ensure(cell);entries[cell].resolved=resolved;entries[cell].cached_generation=generation;
    }
    void clear(){
        if(generation==UINT32_MAX){std::fill(entries.begin(),entries.end(),entry{});generation=1;}
        else ++generation;
    }
};
}
