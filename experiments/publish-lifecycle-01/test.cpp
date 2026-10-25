#include "project/shm/runtime_range_policy.hpp"
#define main compiled_suite_main
#include "../../tests/compiled_project_tests.cpp"
#undef main
int main() {
 for (bool reference : {true, false}) {
  cw::server::detail::runtime_range_reference = reference;
  if (compiled_suite_main() != 0) return 1;
  std::cout << "runtime range reference=" << reference << ", validation=passed\n";
 }
 return 0;
}
