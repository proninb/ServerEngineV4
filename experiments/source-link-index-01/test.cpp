#include "project/graph/link_index_policy.hpp"
#define main compiled_suite_main
#include "../../tests/compiled_project_tests.cpp"
#undef main
int main() {
    for (bool reference : {true, false}) {
        cw::server::detail::link_index_growth_reference = reference;
        cw::server::detail::fail_next_link_wide_allocation = !reference;
        if (compiled_suite_main() != 0) return 1;
        if (cw::server::detail::fail_next_link_wide_allocation) return 2;
        std::cout << "link index reference=" << reference << ", validation=passed\n";
    }
    return 0;
}
