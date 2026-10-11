#include "benchmarks/v337r_owner_ranges.hpp"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

struct row { std::uint32_t owner; };
struct range { std::uint32_t declaration_first, declaration_count; };
struct model {std::vector<row> declarations; std::vector<range> records;};
static void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
static std::size_t reference_count(const model& m, std::size_t ri) {
    const auto& r=m.records[ri]; std::size_t n=0;
    for(std::size_t di=r.declaration_first;di<r.declaration_first+r.declaration_count;++di)
        n+=m.declarations[di].owner==ri;
    return n;
}
int main() {
    try {
        // 0 = outer, 1 = inner, 2 = grandchild, 3 = empty sibling.
        model m{{{0},{1},{2},{2},{1},{0},{0}}, {{0,7},{1,4},{2,2},{7,0}}};
        require(v337r_ranges::owned_declaration_count(m,0)==3,"outer overcounts nested");
        require(v337r_ranges::owned_declaration_count(m,1)==2,"inner count");
        require(v337r_ranges::owned_declaration_count(m,2)==2,"grandchild count");
        require(v337r_ranges::owned_declaration_count(m,3)==0,"empty count");
        require(v337r_ranges::next_after_foreign(m,1,7)==5,"nested skip");
        require(v337r_ranges::next_after_foreign(m,2,7)==4,"grandchild skip");
        // Malformed/out-of-subtree owner: advance exactly one, never overshoot.
        require(v337r_ranges::next_after_foreign(m,2,3)==3,"bounded skip");
        // Generated valid DFS trees, deterministic, compare helper to dense oracle.
        for(std::size_t trial=0;trial<1500;++trial){
            model x; x.records.push_back({0,0});
            std::uint32_t state=static_cast<std::uint32_t>(trial*747796405u+2891336453u);
            const auto next=[&]()->std::uint32_t {state=state*1664525u+1013904223u;return state;};
            const auto visit=[&](auto&& self,std::size_t owner,std::size_t depth)->void {
                x.records[owner].declaration_first=static_cast<std::uint32_t>(x.declarations.size());
                for(unsigned i=0;i<7;++i){
                    if(depth<4 && (next()%7u)==0u){
                        const auto id=x.records.size();
                        x.records.push_back({0,0});
                        self(self,id,depth+1);
                    } else x.declarations.push_back({static_cast<std::uint32_t>(owner)});
                }
                x.records[owner].declaration_count=static_cast<std::uint32_t>(
                    x.declarations.size()-x.records[owner].declaration_first);
            };
            visit(visit,0,0);
            for(std::size_t i=0;i<x.records.size();++i)
                require(v337r_ranges::owned_declaration_count(x,i)==reference_count(x,i),
                    "generated owner count mismatch");
        }
        std::cout<<"V337R_RANGES_PASS\n";
        return EXIT_SUCCESS;
    }catch(const std::exception& e){std::cerr<<"V337R_RANGES_FAIL "<<e.what()<<'\n'; return EXIT_FAILURE;}
}
