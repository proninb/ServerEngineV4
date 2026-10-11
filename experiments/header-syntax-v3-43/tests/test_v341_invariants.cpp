#include <stdexcept>
#include <iostream>
#include "v341_fields_invariants.hpp"
int main(){
  using namespace v341_diag;
  if(!fields(19,19,19,19,19,17,2,2))throw std::runtime_error("V341 fields partition FAILED");
  if(fields(19,19,18,19,19,17,2,2))throw std::runtime_error("V341 accepted invalid append count");
  if(fields(19,19,19,19,19,17,20,1))throw std::runtime_error("V341 accepted invalid ref count");
  std::cout<<"V341_FIELDS_INVARIANTS_PASS\n";
}
