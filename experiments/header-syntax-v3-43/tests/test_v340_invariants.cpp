#include "benchmarks/v340_profile_invariants.hpp"
#include <stdexcept>
#include <iostream>
static void must(bool b){if(!b)throw std::runtime_error("V340 counters FAILED");}
int main(){
    // Tests never rely on assert(), including Release builds with NDEBUG.
    must(v340_diag::named(17,9,5,3));
    must(!v340_diag::named(17,9,6,3));
    must(!v340_diag::named(17,18,0,0));
    must(v340_diag::fallback(15,4,2,3,6,8,7));
    must(!v340_diag::fallback(15,4,2,3,5,8,7));
    must(!v340_diag::fallback(15,4,2,3,6,9,7));
    must(v340_diag::shape(20,1,7,9,3));
    must(!v340_diag::shape(20,1,7,8,3));
    std::cout<<"V340_COUNTER_PARTITIONS_PASS\n";
}
