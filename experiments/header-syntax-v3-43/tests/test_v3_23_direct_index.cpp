#define CW_V323_INDEX_TEST 1
#include "../benchmarks/v3_23_direct_index.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
int main(){
    v323_index::direct_id_index idx;
    using X=v323_index::direct_id_index;
    assert(idx.find(5)==X::not_found);
    idx.begin_record();
    assert(idx.insert(5,0));assert(idx.insert(123456,1));
    assert(idx.find(5)==0 && idx.find(123456)==1);
    assert(!idx.insert(5,2));
    idx.begin_record();assert(idx.find(5)==X::not_found);
    assert(idx.insert(123456,0));assert(idx.find(123456)==0);
    assert(idx.find(0)==X::not_found);
    idx.test_set_epoch(UINT32_MAX);
    idx.begin_record();assert(idx.find(123456)==X::not_found);
    assert(idx.insert(5,3));assert(idx.find(5)==3);
    for(std::uint32_t n=1;n<1000;++n){
        idx.begin_record();
        assert(idx.insert(n, n));assert(idx.find(n)==n);
        assert(idx.find(5)==(n==5?5:X::not_found));
    }
    // A bogus sparse string ID fails closed, never allocates tens of GiB.
    assert(!idx.insert(UINT32_MAX,1));
    std::cout<<"V3-23 direct_id_index PASS\n";
}
