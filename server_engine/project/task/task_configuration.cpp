#include "task_configuration.hpp"
#include "../../json/json_parser.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cw::server {

bool task_is_scheduled(const task_settings& settings,
    std::uint64_t cycle, bool frozen) noexcept {
    return settings.calls != 0 && settings.cycles != 0 &&
        settings.starting_from_cycle != 0 &&
        cycle >= settings.starting_from_cycle &&
        (!frozen || settings.run_in_freeze) &&
        (cycle - settings.starting_from_cycle) % settings.cycles == 0;
}

bool validate_task_configuration(task_configuration& configuration,
    std::string& error) {
    error.clear();
    if (configuration.timing.period_ms != 0 &&
        configuration.timing.period_ms != 1 &&
        configuration.timing.period_ms != 2) {
        error = "Task timing.period_ms must be 1 or 2";
        return false;
    }
    configuration.execution_order.clear();
    std::unordered_map<std::string, std::size_t> ids;
    for (std::size_t i = 0; i < configuration.tasks.size(); ++i) {
        const auto& task = configuration.tasks[i];
        if (task.id.empty() || task.executable.empty()) {
            error = "Task id and executable must not be empty";
            return false;
        }
        if (!ids.emplace(task.id, i).second) {
            error = "Duplicate Task id: " + task.id;
            return false;
        }
        if (!task.settings.calls || !task.settings.cycles ||
            !task.settings.starting_from_cycle) {
            error = "Invalid Task schedule: " + task.id;
            return false;
        }
    }
    std::vector<unsigned char> state(configuration.tasks.size(), 0);
    std::function<bool(std::size_t)> visit = [&](std::size_t index) {
        if (state[index] == 1) {
            error = "Circular Task dependency: " + configuration.tasks[index].id;
            return false;
        }
        if (state[index] == 2) return true;
        state[index] = 1;
        std::unordered_set<std::string> seen;
        for (const auto& dependency : configuration.tasks[index].dependencies) {
            if (!seen.insert(dependency).second) {
                error = "Duplicate Task dependency: " + dependency;
                return false;
            }
            const auto found = ids.find(dependency);
            if (found == ids.end()) {
                error = "Unknown Task dependency: " + dependency;
                return false;
            }
            if (!visit(found->second)) return false;
        }
        state[index] = 2;
        configuration.execution_order.push_back(index);
        return true;
    };
    for (std::size_t i = 0; i < configuration.tasks.size(); ++i) {
        if (!visit(i)) {
            configuration.execution_order.clear();
            return false;
        }
    }
    return true;
}

namespace {
enum class kind { root, timing, diagnostics, deadline, time_drift, tasks, task, dependencies, settings };
struct frame {
    kind type;
    std::unordered_set<std::string> keys;
    std::string pending;
    bool awaiting = false;
    bool version = false;
    bool tasks = false;
    bool id = false;
    bool executable = false;
    bool settings = false;
    bool calls = false;
    bool cycles = false;
    bool starting = false;
};

class handler final : public json_event_handler {
public:
    explicit handler(task_configuration& output) : output(output) {}
    void object_begin() override {
        if (!error.empty()) return;
        if (stack.empty()) {
            if (root_started) return fail("Multiple root objects");
            root_started = true;
            stack.push_back({kind::root});
            return;
        }
        auto& parent = stack.back();
        if (parent.type == kind::root && parent.awaiting && parent.pending == "timing") {
            parent.awaiting = false;
            stack.push_back({kind::timing});
        } else if (parent.type == kind::timing && parent.awaiting &&
                   parent.pending == "diagnostics") {
            parent.awaiting = false;
            stack.push_back({kind::diagnostics});
        } else if (parent.type == kind::diagnostics && parent.awaiting &&
                   (parent.pending == "deadline" || parent.pending == "time_drift")) {
            const auto type = parent.pending == "deadline" ? kind::deadline : kind::time_drift;
            parent.awaiting = false;
            stack.push_back({type});
        } else if (parent.type == kind::tasks) {
            output.tasks.emplace_back();
            stack.push_back({kind::task});
        } else if (parent.type == kind::task &&
            parent.awaiting && parent.pending == "settings") {
            parent.awaiting = false;
            parent.settings = true;
            stack.push_back({kind::settings});
        } else {
            fail("Unexpected object");
        }
    }
    void object_end() override {
        if (!error.empty()) return;
        if (stack.empty()) return fail("Unexpected object end");
        const auto& f = stack.back();
        if (f.awaiting) return fail("Missing object value");
        if (f.type == kind::root && (!f.version || !f.tasks))
            return fail("Root requires version and tasks");
        if (f.type == kind::task && (!f.id || !f.executable))
            return fail("Task requires id and executable");
        if (f.type == kind::settings &&
            (!f.calls || !f.cycles || !f.starting))
            return fail("Settings requires calls, cycles and starting_from_cycle");
        if (f.type == kind::tasks || f.type == kind::dependencies)
            return fail("Unexpected object end");
        stack.pop_back();
    }
    void array_begin() override {
        if (!error.empty()) return;
        if (stack.empty()) return fail("Unexpected array");
        auto& parent = stack.back();
        if (parent.type == kind::root && parent.awaiting &&
            parent.pending == "tasks") {
            parent.awaiting = false;
            parent.tasks = true;
            stack.push_back({kind::tasks});
        } else if (parent.type == kind::task && parent.awaiting &&
            parent.pending == "dependencies") {
            parent.awaiting = false;
            stack.push_back({kind::dependencies});
        } else {
            fail("Unexpected array");
        }
    }
    void array_end() override {
        if (!error.empty()) return;
        if (stack.empty() ||
            (stack.back().type != kind::tasks &&
             stack.back().type != kind::dependencies))
            return fail("Unexpected array end");
        stack.pop_back();
    }
    void key(std::string_view key) override {
        if (!error.empty()) return;
        if (stack.empty()) return fail("Unexpected key");
        auto& f = stack.back();
        if (f.type == kind::tasks || f.type == kind::dependencies || f.awaiting)
            return fail("Unexpected key");
        std::string name(key);
        if (!f.keys.insert(name).second)
            return fail("Duplicate JSON key: " + name);
        const bool known = f.type == kind::root ?
            (name == "version" || name == "tasks" || name == "timing") :
            f.type == kind::timing ?
            (name == "mode" || name == "period_ms" || name == "diagnostics") :
            f.type == kind::diagnostics ?
            (name == "enabled" || name == "deadline" || name == "time_drift") :
            f.type == kind::deadline ?
            (name == "enabled" || name == "warn_over_ms" || name == "stop_over_ms") :
            f.type == kind::time_drift ?
            (name == "enabled" || name == "warn_ahead_ms" || name == "warn_behind_ms") :
            f.type == kind::task ?
            (name == "id" || name == "executable" ||
             name == "dependencies" || name == "settings" ||
             name == "enabled") :
            (name == "calls" || name == "cycles" ||
             name == "starting_from_cycle" || name == "run_in_freeze");
        if (!known) return fail("Unknown JSON key: " + name);
        f.pending = std::move(name);
        f.awaiting = true;
    }
    void value(json_value_view value) override {
        if (!error.empty()) return;
        if (stack.empty()) return fail("Unexpected value");
        auto& f = stack.back();
        if (f.type == kind::dependencies) {
            std::string dependency;
            if (!value.get(dependency) || dependency.empty())
                return fail("Dependency must be a non-empty string");
            output.tasks.back().dependencies.push_back(std::move(dependency));
            return;
        }
        if (!f.awaiting) return fail("Unexpected scalar value");
        f.awaiting = false;
        const auto& name = f.pending;
        if (f.type == kind::root) {
            if (name != "version") return fail("Expected timing object or tasks array");
            std::uint32_t version = 0;
            if (!value.get(version) || version != 1)
                return fail("Unsupported Tasks.json version");
            f.version = true;
        } else if (f.type == kind::timing) {
            if (name == "mode") {
                std::string mode;
                if (!value.get(mode)) return fail("timing.mode must be a string");
                if (mode == "NORMAL") output.timing.mode = task_timing_mode::normal;
                else if (mode == "REAL_TIME") output.timing.mode = task_timing_mode::real_time;
                else return fail("Unsupported timing.mode");
            } else if (name == "period_ms") {
                if (!value.get(output.timing.period_ms) ||
                    (output.timing.period_ms != 1 && output.timing.period_ms != 2))
                    return fail("timing.period_ms must be 1 or 2");
            } else return fail("Expected timing.diagnostics object");
        } else if (f.type == kind::diagnostics) {
            if (name != "enabled" || !value.get(output.timing.diagnostics.enabled))
                return fail("Invalid diagnostics.enabled");
        } else if (f.type == kind::deadline) {
            auto& d = output.timing.diagnostics.deadline;
            if (name == "enabled") {
                if (!value.get(d.enabled)) return fail("Invalid deadline.enabled");
            } else if (name == "warn_over_ms") {
                if (!milliseconds_to_ns(value, d.warn_over_ns)) return fail("Invalid warn_over_ms");
            } else if (name == "stop_over_ms") {
                if (!milliseconds_to_ns(value, d.stop_over_ns)) return fail("Invalid stop_over_ms");
            }
        } else if (f.type == kind::time_drift) {
            auto& d = output.timing.diagnostics;
            if (name == "enabled") {
                if (!value.get(d.time_drift_enabled)) return fail("Invalid time_drift.enabled");
            } else if (name == "warn_ahead_ms") {
                if (!milliseconds_to_ns(value, d.warn_ahead_ns)) return fail("Invalid warn_ahead_ms");
            } else if (name == "warn_behind_ms") {
                if (!milliseconds_to_ns(value, d.warn_behind_ns)) return fail("Invalid warn_behind_ms");
            }
        } else if (f.type == kind::task) {
            auto& task = output.tasks.back();
            if (name == "id") {
                if (!value.get(task.id)) return fail("Invalid Task id");
                f.id = true;
            } else if (name == "executable") {
                if (!value.get(task.executable)) return fail("Invalid Task executable");
                f.executable = true;
            } else if (name == "enabled") {
                if (!value.get(task.enabled)) return fail("Invalid Task enabled");
            } else {
                return fail("Expected Task array/object value");
            }
        } else if (f.type == kind::settings) {
            auto& settings = output.tasks.back().settings;
            if (name == "calls") {
                if (!value.get(settings.calls) || !settings.calls)
                    return fail("Invalid calls");
                f.calls = true;
            } else if (name == "cycles") {
                if (!value.get(settings.cycles) || !settings.cycles)
                    return fail("Invalid cycles");
                f.cycles = true;
            } else if (name == "starting_from_cycle") {
                if (!value.get(settings.starting_from_cycle) ||
                    !settings.starting_from_cycle)
                    return fail("Invalid starting_from_cycle");
                f.starting = true;
            } else if (name == "run_in_freeze") {
                if (!value.get(settings.run_in_freeze))
                    return fail("Invalid run_in_freeze");
            } else {
                return fail("Unknown Task setting");
            }
        } else {
            fail("Unexpected scalar in array");
        }
    }
    static bool milliseconds_to_ns(json_value_view value, std::int64_t& target) {
        double milliseconds = 0;
        if (!value.get(milliseconds) || milliseconds < 0 || milliseconds > 86400000)
            return false;
        target = static_cast<std::int64_t>(milliseconds * 1000000.0 + 0.5);
        return true;
    }
    void fail(std::string message) { if (error.empty()) error = std::move(message); }
    task_configuration& output;
    std::vector<frame> stack;
    std::string error;
    bool root_started = false;
};
}

bool parse_task_configuration(std::string_view json,
    task_configuration& output, std::string& error) {
    output = {};
    error.clear();
    handler reader(output);
    const auto parsed = parse_json(json, reader);
    if (!parsed.ok()) error = std::string(json_error_message(parsed.code));
    else if (!reader.error.empty()) error = reader.error;
    else if (!reader.root_started || !reader.stack.empty())
        error = "Incomplete Tasks.json";
    if (error.empty() && !validate_task_configuration(output, error)) {}
    if (!error.empty()) { output = {}; return false; }
    return true;
}
}
