#include "project/parser/source_string_scratch_policy.hpp"
#include <cstdlib>
#define main frontend_regression_main
#include "../../tests/frontend_regression_tests.cpp"
#undef main
int main() {
#ifdef CW_SOURCE_STRING_SCRATCH_AB
    const auto* value = std::getenv("CW_SOURCE_STRING_SCRATCH");
    cw::server::detail::source_string_scratch_reuse = value && std::string_view{value} == "1";
#endif
    return frontend_regression_main();
}
