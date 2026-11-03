#include "../server_engine/project/task/task_configuration.hpp"
#include <cstdio>
#include <string>
using namespace cw::server;
int main() {
    const char* bad[] = {
        R"({"version":1})",
        R"({"tasks":[]})",
        R"({"version":1,"tasks":[],"tasks":[]})",
        R"({"version":1,"tasks":[{"id":"a","executable":"a.exe","id":"b"}]})",
        R"({"version":1,"tasks":[{"id":"a"}]})",
        R"({"version":1,"tasks":[{"id":"a","executable":"a.exe","settings":{"calls":2}}]})",
        R"({"version":1,"tasks":[{"id":"a","executable":"a.exe","settings":{"calls":0,"cycles":1,"starting_from_cycle":1}}]})",
        R"({"version":1,"tasks":[{"id":"a","executable":"a.exe","dependencies":["missing"]}]})",
        R"({"version":1,"tasks":[{"id":"a","executable":"a.exe","dependencies":["a"]}]})",
        R"({"version":1,"tasks":[{"id":"a","executable":"a.exe","dependencies":{}}]})",
        R"({"version":1,"tasks":[{"id":"a","executable":"a.exe","enabled":42}]})"
    };
    for (unsigned i = 0; i < sizeof(bad)/sizeof(*bad); ++i) {
        task_configuration cfg; std::string error;
        if (parse_task_configuration(bad[i], cfg, error) || error.empty()) {
            std::fprintf(stderr, "Invalid JSON accepted: case %u\n", i);
            return 1;
        }
    }
    task_configuration cfg; std::string error;
    if (!parse_task_configuration(
        R"({"version":1,"tasks":[{"id":"a","executable":"a.exe"}]})",
        cfg, error) || cfg.tasks.size() != 1 || cfg.execution_order.size() != 1) {
        std::fprintf(stderr, "Valid JSON rejected: %s\n", error.c_str());
        return 2;
    }
    return 0;
}
