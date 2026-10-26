#include "../parallel-local-source-02/bind.hpp"
#include "registry.hpp"
#include <iostream>
#include <stdexcept>
void check(bool value){if(!value)throw std::runtime_error("Lazy direct-key registry invariant failed");}
int main(){
 using namespace pls02;struct cell {identity_ref who;type_ref type;range steps;};
 std::vector<endpoint_path_step> paths{{0,endpoint_path_step_kind::member,{}},{1,endpoint_path_step_kind::member,{}}};
 const auto who=identity_ref::from_raw(7);const auto type=type_ref{};
 pls10b::registry index;
 const auto first=index.direct(who,type,{0,1},0,paths);check(first.first==0&&first.second==0);
 const auto duplicate=index.direct(who,type,{0,1},9,paths);check(duplicate==first);
 const auto second=index.direct(who,type,{1,1},1,paths);check(second.first==1&&second.second==1);
 std::vector<cell> cells;
 // Pre-discovered direct keys do not force canonical creation order.
 check(index.intern(who,type,{1,1},cells,paths)==0);
 check(index.materialize(first.first,cells)==1);
 check(index.materialize(second.first,cells)==0);
 check(cells.size()==2&&cells[0].steps.begin==1&&cells[1].steps.begin==0);
 index.release_keys();check(index.intern(who,type,{0,1},cells,paths)==1);
 check(index.intern(who,type,{1,1},cells,paths)==0);
 std::cout<<"lazy direct-key registry preserves deduplication, cell order and lookup after key release\n";
}
