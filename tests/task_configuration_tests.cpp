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

    // TASK-TIMING-SHM-01: NORMAL/REAL_TIME and independent deadline settings.
    const char* timing = R"({"version":1,"timing":{"mode":"REAL_TIME","period_ms":1,
      "diagnostics":{"enabled":true,"deadline":{"enabled":true,
      "warn_over_ms":0.2,"stop_over_ms":1.0},"time_drift":{
      "enabled":true,"warn_ahead_ms":50,"warn_behind_ms":50}}},"tasks":[]})";
    CHECK(parse_task_configuration(timing, configuration, error));
    CHECK(configuration.timing.mode == task_timing_mode::real_time);
    CHECK(configuration.timing.period_ms == 1);
    CHECK(configuration.timing.diagnostics.deadline.stop_over_ns == 1000000);
    CHECK(configuration.timing.diagnostics.deadline.warn_over_ns == 200000);
    CHECK(configuration.timing.diagnostics.warn_behind_ns == 50000000);

    const char* normal = R"({"version":1,"tasks":[]})";
    CHECK(parse_task_configuration(normal, configuration, error));
    CHECK(configuration.timing.mode == task_timing_mode::normal);
    CHECK(configuration.timing.diagnostics.deadline.stop_over_ns == -1);

    const char* bad_mode = R"({"version":1,"timing":{"mode":"HARD_REAL_TIME"},"tasks":[]})";
    CHECK(!parse_task_configuration(bad_mode, configuration, error));
    const char* bad_threshold = R"({"version":1,"timing":{"diagnostics":{
      "deadline":{"stop_over_ms":-1}}},"tasks":[]})";
    CHECK(!parse_task_configuration(bad_threshold, configuration, error));

    return 0;
}

