#include "project/string/intern_slot_policy.hpp"
#define main compiled_suite_main
#include "../../tests/compiled_project_tests.cpp"
#undef main
int main() {
    for (bool reference : {true, false}) {
        cw::server::detail::string_intern_slot_reference = reference;
        if (compiled_suite_main() != 0) return 1;
        cw::server::string_table table;
        std::vector<cw::server::string_id> ids;
        for (unsigned i=0; i<1024; ++i) {
            cw::server::string_id id;
            if (!cw::server::succeeded(table.intern_prehashed("collision_" + std::to_string(i), 17, id))) return 2;
            ids.push_back(id);
        }
        for (unsigned i=0; i<ids.size(); ++i) {
            const auto name = "collision_" + std::to_string(i);
            cw::server::string_id id;
            if (table.find_prehashed(name,17)!=ids[i] || !cw::server::succeeded(table.intern_prehashed(name,17,id)) || id!=ids[i]) return 3;
        }
        std::cout << "intern slot reference=" << reference << ", validation=passed\n";
    }
    return 0;
}
