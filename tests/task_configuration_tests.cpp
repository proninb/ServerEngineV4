#include "../server_engine/project/task/task_configuration.hpp"

#include <cstdlib>
#include <string>

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            std::abort(); \
        } \
    } while (false)

int main() {
    using namespace cw::server;

    task_configuration configuration;
    std::string error;

    const char* valid = R"({
        "version": 1,
        "tasks": [
            {
                "id": "Input",
                "executable": "Input.exe",
                "dependencies": [],
                "settings": {
                    "calls": 3,
                    "cycles": 5,
                    "starting_from_cycle": 1,
                    "run_in_freeze": false
                }
            },
            {
                "id": "Controller",
                "executable": "Controller.exe",
                "dependencies": ["Input"],
                "settings": {
                    "calls": 1,
                    "cycles": 1,
                    "starting_from_cycle": 1,
                    "run_in_freeze": true
                }
            }
        ]
    })";

    CHECK(parse_task_configuration(
        valid, configuration, error));

    CHECK(configuration.tasks.size() == 2);
    CHECK(configuration.execution_order.size() == 2);
    CHECK(configuration.execution_order[0] == 0);
    CHECK(configuration.execution_order[1] == 1);

    const auto& input = configuration.tasks[0].settings;

    CHECK(task_is_scheduled(input, 1, false));
    CHECK(!task_is_scheduled(input, 2, false));
    CHECK(task_is_scheduled(input, 6, false));
    CHECK(!task_is_scheduled(input, 6, true));

    configuration.tasks[0].dependencies = {"Controller"};

    CHECK(!validate_task_configuration(configuration, error));

    return 0;
}

