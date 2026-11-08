/*
 * Server request dispatch and lifecycle implementation.
 *
 * Every externally visible Server request owns one operation_id and one
 * diagnostic_collection. Nested layers append to that same collection.
 */
#include "server.hpp"
// TASK_SCHEDULER_V2_01: opt-in precision timer; no permanent system timer change.
#include <cstdlib>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "project/task/runtime/task_perf_probe.hpp"
#include "fixed_shared_memory.hpp"
#include "project/task/runtime/task_process.hpp"
#if defined(CW_UNLOAD_HEAP_OPTIMIZATION)
#include "unload_heap_optimization.hpp"
#endif

#include "configuration/server_configuration_loader.hpp"
#include "diagnostics/diagnostic_builder.hpp"
#include "diagnostics/diagnostic_descriptor.hpp"
#include "filesystem_path.hpp"
#include "license/server_license_loader.hpp"
#include "license/server_lease_loader.hpp"
#include "project/project_build.hpp"
#include "project/project_load.hpp"
#include "project/project_configuration_loader.hpp"
#include "project/ic/ic_catalog.hpp"
#include "project/project_publish.hpp"
#include "project/project_rebuild.hpp"
#include "project/runtime/runtime_ic_codec.hpp"
#include "project/runtime/runtime_ic_reset.hpp"
#include "project/runtime/runtime_ic_snapshot.hpp"
#include "project/runtime/runtime_query.hpp"
#include "read_only_file_mapping.hpp"
#include "writable_file_mapping.hpp"

#include <chrono>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <system_error>
#include <vector>

namespace cw::server {
namespace {

inline constexpr auto shutdown_ack_timeout =
    std::chrono::milliseconds{2000};

[[nodiscard]] std::filesystem::path resolve(
    const std::filesystem::path& base,
    const std::filesystem::path& path) {

    return path.is_absolute()
        ? path.lexically_normal()
        : (base / path).lexically_normal();
}

[[nodiscard]] server_status load_resident_project_configuration(
    project& value,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    project_runtime_configuration configuration;

    const auto status =
        load_project_runtime_configuration(
            value.path(),
            operation,
            diagnostics,
            configuration);

    if (!succeeded(status)) {
        return status;
    }

    value.set_task_configuration(std::move(configuration.task_definitions));
    ic_catalog catalog;

    if (configuration.ic.empty()) {
        value.set_ic_catalog(
            std::move(catalog));
        return server_status::success;
    }

    const auto catalog_path =
        resolve(
            value.path().parent_path(),
            configuration.ic);

    value.set_ic_catalog_path(
        catalog_path);

    const auto loaded =
        load_ic_catalog(
            catalog_path,
            catalog);

    if (loaded == ic_catalog_result::not_found) {
        value.set_ic_catalog(
            std::move(catalog));
        return server_status::success;
    }

    if (loaded != ic_catalog_result::success) {
        diagnostics.emit(
            diagnostic(
                loaded == ic_catalog_result::invalid
                    ? diagnostics::runtime_ic_invalid
                    : diagnostics::runtime_ic_io_failed,
                operation)
                .file(catalog_path)
                .detail(
                    loaded == ic_catalog_result::invalid
                        ? "Configured IC.json is invalid"
                        : "Configured IC.json could not be read")
                .build());

        return loaded == ic_catalog_result::invalid
            ? server_status::runtime_ic_invalid
            : server_status::io_error;
    }

    value.set_ic_catalog(
        std::move(catalog));

    return server_status::success;
}

[[nodiscard]] server_status require_ic_catalog(
    const project& value,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (!value.ic_catalog_path().empty()) {
        return server_status::success;
    }

    diagnostics.emit(
        diagnostic(
            diagnostics::runtime_ic_invalid,
            operation)
            .detail(
                "Root project.json does not configure an IC catalog path")
            .build());

    return server_status::runtime_ic_invalid;
}

}

operation_id server::next_operation() noexcept {
    const auto value = next_operation_value++;

    // uint64 wrap is practically unreachable, but zero remains reserved.
    if (next_operation_value == 0) {
        next_operation_value = 1;
    }

    return operation_id{value};
}

server_status server::start(
    const std::filesystem::path& configuration_path,
    diagnostic_collection& diagnostics) {

    diagnostics.clear();

    const auto operation = next_operation();

    context.configuration_path =
        std::filesystem::absolute(
            configuration_path).lexically_normal();

    context.configuration_directory =
        context.configuration_path.parent_path();

    auto status = load_server_configuration(
        context.configuration_path,
        operation,
        diagnostics,
        context.configuration);

    if (!succeeded(status)) {
        return status;
    }

    status = load_server_license(
        context.configuration_directory /
            "server.license",
        std::chrono::system_clock::now(),
        operation,
        diagnostics,
        context.license);

    if (!succeeded(status)) {
        return status;
    }

    status = context.identity.start(
        context.configuration.server_identity,
        std::chrono::system_clock::now());

    if (!succeeded(status)) {
        const auto& descriptor =
            status == server_status::unsupported
                ? diagnostics::server_identity_unsupported
                : diagnostics::server_identity_start_failed;

        diagnostics.emit(
            diagnostic(
                descriptor,
                operation)
                .detail(
                    context.identity.detail().empty()
                        ? "Failed to establish Server identity"
                        : std::string(context.identity.detail()))
                .build());

        context.identity.stop();
        context.license.clear();
        return status;
    }

    status = load_server_lease(
        context.configuration_directory /
            "server.lease",
        std::chrono::system_clock::now(),
        context.license,
        operation,
        diagnostics,
        context.lease);

    if (!succeeded(status)) {
        context.identity.stop();
        context.license.clear();
        return status;
    }

    status = context.authentication.start(
        context.configuration.authentication,
        context.configuration_directory);

    if (succeeded(status)) {
        context.mode =
            server_mode::full;
    } else {
        const auto detail =
            context.authentication.detail().empty()
                ? std::string("Authentication unavailable")
                : std::string(context.authentication.detail());

        context.authentication.stop();
        context.mode =
            server_mode::demo;

        diagnostics.emit(
            diagnostic(
                diagnostics::server_demo_mode,
                operation)
                .detail(detail)
                .build());
    }

    status = context.communications.start(
        context.configuration.communication,
        context.requests,
        context.mode,
        context.lease.limits().max_connections);

    if (!succeeded(status)) {
        const auto& descriptor =
            status == server_status::unsupported
                ? diagnostics::communication_unsupported_transport
                : diagnostics::communication_start_failed;

        diagnostics.emit(
            diagnostic(
                descriptor,
                operation)
                .detail(
                    status == server_status::unsupported
                        ? "Configured communication transport has no backend"
                        : "Failed to start configured communication endpoints")
                .build());

        context.authentication.stop();
        context.identity.stop();
        return status;
    }

    running = true;

    if (context.configuration.project) {
        const auto& startup =
            *context.configuration.project;

        switch (startup.startup) {
        case project_startup_mode::load:
            status = load(
                startup.path,
                operation,
                diagnostics);
            break;

        case project_startup_mode::publish:
            status = publish(
                startup.path,
                operation,
                diagnostics);
            break;

        case project_startup_mode::rebuild:
            status = rebuild(
                startup.path,
                operation,
                diagnostics);
            break;
        }

        if (!succeeded(status)) {
            shutdown();
            return status;
        }
    }

    return server_status::success;
}

namespace {
// Timer resolution is requested only while the server run loop is active.
class scheduler_timer_resolution final {
public:
    scheduler_timer_resolution() noexcept {
        const char* enabled = std::getenv("TASK_SCHEDULER_V2");
        if (!enabled || enabled[0] != '1' || enabled[1] != '\0') return;
        module = LoadLibraryW(L"winmm.dll");
        if (!module) return;
        begin = reinterpret_cast<timer_function>(GetProcAddress(module, "timeBeginPeriod"));
        end = reinterpret_cast<timer_function>(GetProcAddress(module, "timeEndPeriod"));
        if (begin && end && begin(1) == 0) active = true;
    }
    ~scheduler_timer_resolution() noexcept {
        if (active) end(1);
        if (module) FreeLibrary(module);
    }
    scheduler_timer_resolution(const scheduler_timer_resolution&) = delete;
    scheduler_timer_resolution& operator=(const scheduler_timer_resolution&) = delete;
    bool enabled() const noexcept { return active; }
private:
    using timer_function = unsigned int (WINAPI*)(unsigned int);
    HMODULE module = nullptr;
    timer_function begin = nullptr;
    timer_function end = nullptr;
    bool active = false;
};
}

namespace {
class scheduler_hr_timer final {
public:
    scheduler_hr_timer() noexcept {
        handle = CreateWaitableTimerExW(nullptr, nullptr,
            CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_MODIFY_STATE | SYNCHRONIZE);
        if (!handle) handle = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    }
    ~scheduler_hr_timer() noexcept { if (handle) CloseHandle(handle); }
    scheduler_hr_timer(const scheduler_hr_timer&) = delete;
    scheduler_hr_timer& operator=(const scheduler_hr_timer&) = delete;
    bool available() const noexcept { return handle != nullptr; }
    bool wait(HANDLE queue_event, std::chrono::steady_clock::time_point target) const noexcept {
        if (!handle || !queue_event) return false;
        auto remaining = target - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::steady_clock::duration::zero()) return true;
        auto units = std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count() / 100;
        if (units < 1) units = 1;
        LARGE_INTEGER due{};
        due.QuadPart = -units;
        if (!SetWaitableTimer(handle, &due, 0, nullptr, nullptr, FALSE)) return false;
        HANDLE waits[] = {queue_event, handle};
        const DWORD result = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        CancelWaitableTimer(handle);
        return result == WAIT_OBJECT_0 + 1;
    }
private:
    HANDLE handle = nullptr;
};
}

int server::run() {
    // TASK_SCHEDULER_DEADLINE_01: optional per-cycle diagnostics, no scheduling changes.
    const char* deadline_enabled = std::getenv("TASK_SCHEDULER_DEADLINE");
    const bool deadline_diagnostics = deadline_enabled &&
        deadline_enabled[0] == '1' && deadline_enabled[1] == '\0';
    std::FILE* deadline_file = nullptr;
    if (deadline_diagnostics) {
        const char* output = std::getenv("TASK_SCHEDULER_DEADLINE_CSV");
        if (!output || !output[0]) output = "task-scheduler-deadline.csv";
        if (fopen_s(&deadline_file, output, "a+") != 0 || !deadline_file) {
            std::fprintf(stderr, "Deadline diagnostics: cannot open %s\n", output);
        } else {
            // E2E-02 restarts the server three times per mode. Keep all sessions.
            // Write the header only for a new/empty file.
            std::fseek(deadline_file, 0, SEEK_END);
            const auto existing_bytes = std::ftell(deadline_file);
            if (existing_bytes == 0) {
                std::fputs("runtime_tick,scheduled_ns,wakeup_ns,dispatch_ns,barrier_complete_ns,cycle_complete_ns,wakeup_lateness_us,barrier_duration_us,cycle_duration_us,deadline_miss,overrun_us,skipped_periods\n", deadline_file);
            }
        }
    }
    const auto deadline_origin = std::chrono::steady_clock::now();
    const auto deadline_ns = [&](std::chrono::steady_clock::time_point point) {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            point - deadline_origin).count();
    };
    const scheduler_timer_resolution precision_timer;
    const scheduler_hr_timer hr_timer;
    const auto env_equal = [](const char* name, const char* value) {
        const char* found = std::getenv(name);
        return found && std::strcmp(found, value) == 0;
    };
    const bool hr_enabled = env_equal("TASK_SCHEDULER_TIMER", "HRTIMER") ||
        env_equal("TASK_SCHEDULER_TIMER", "HYBRID");
    const bool hybrid = env_equal("TASK_SCHEDULER_TIMER", "HYBRID");
    const bool overlap = env_equal("TASK_SCHEDULER_EXECUTION", "OVERLAP");
    const auto spin_budget = std::chrono::microseconds(150);
    const HANDLE queue_event = hr_enabled ? context.requests.native_wake_event() : nullptr;
    // TASK_SHM_SCHEDULER_08: opt-in benchmark period; production default remains 2 ms.
    int scheduler_period_ms = 2;
    if (const char* value = std::getenv("TASK_SCHEDULER_PERIOD_MS")) {
        if (std::strcmp(value, "1") == 0) scheduler_period_ms = 1;
        else if (std::strcmp(value, "2") == 0) scheduler_period_ms = 2;
        else if (std::strcmp(value, "5") == 0) scheduler_period_ms = 5;
        else {
            std::fprintf(stderr, "Invalid TASK_SCHEDULER_PERIOD_MS: expected 1, 2 or 5\n");
            return 1;
        }
    }
    const auto scheduler_period = std::chrono::milliseconds(scheduler_period_ms);
    scheduler_default_period_us = static_cast<std::uint32_t>(scheduler_period_ms * 1000);
    // The thread is parked unless the loaded Project selects realtime or
    // the legacy TASK_SCHEDULER_THREAD opt-in is used.
    scheduler_thread_enabled = true;
    if (scheduler_thread_enabled) {
        scheduler_exit = false;
        scheduler_thread = std::thread([this, scheduler_period, hr_enabled, hybrid, spin_budget,
                                        deadline_file, deadline_origin]() {
            // Dedicated scheduler: never waits on the TCP/control queue event.
            (void)SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
            scheduler_hr_timer timer;
            const HANDLE stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            auto next_tick = std::chrono::steady_clock::now();
            auto previous_tick = task_runtime::perf::clock::now();
            const auto deadline_ns = [deadline_origin](std::chrono::steady_clock::time_point point) {
                return std::chrono::duration_cast<std::chrono::nanoseconds>(point - deadline_origin).count();
            };
            for (;;) {
                bool realtime = false;
                std::chrono::microseconds tick_period{2000};
                {
                    std::unique_lock<std::mutex> lock(scheduler_mutex);
                    scheduler_cv.wait(lock, [this]() {
                        return scheduler_exit || (scheduler_dedicated_active &&
                            context.project && !task_runtime_failed &&
                            (project_state_value == project_state::run ||
                             project_state_value == project_state::freeze));
                    });
                    if (scheduler_exit) break;
                    next_tick = next_task_tick;
                    realtime = scheduler_realtime_active;
                    tick_period = std::chrono::microseconds(scheduler_project_period_us);
                }
                for (;;) {
                    // No control-queue wakeups can disturb this deadline.
                    if (realtime) {
                        // Unpaced: the only wait is the internal Task LOCKSTEP barrier.
                    } else if (hr_enabled && stop_event && timer.available()) {
                        const auto target = hybrid ? next_tick - spin_budget : next_tick;
                        (void)timer.wait(stop_event, target);
                        if (hybrid) {
                            while (std::chrono::steady_clock::now() < next_tick) {
                                if (WaitForSingleObject(stop_event, 0) == WAIT_OBJECT_0) break;
                                YieldProcessor();
                            }
                        }
                    } else {
                        std::this_thread::sleep_until(next_tick);
                    }
                    std::unique_lock<std::mutex> lock(scheduler_mutex);
                    if (scheduler_exit || !scheduler_dedicated_active) break;
                    if (!context.project || task_runtime_failed ||
                        (project_state_value != project_state::run &&
                         project_state_value != project_state::freeze)) break;
                    if (scheduler_resync_requested) {
                        next_tick = next_task_tick;
                        scheduler_resync_requested = false;
                    }
                    const auto actual_wakeup = std::chrono::steady_clock::now();
                    if (!realtime && actual_wakeup < next_tick) continue;
                    const auto scheduled_tick = next_tick;
                    const auto perf_tick_start = task_runtime::perf::clock::now();
                    task_runtime::perf::record("cycle_interval", runtime_tick, previous_tick);
                    previous_tick = perf_tick_start;
                    ++runtime_tick;
                    const bool frozen = project_state_value == project_state::freeze;
                    if (!frozen) ++model_cycle;
                    const auto cycle = frozen ? runtime_tick : model_cycle;
                    const auto task_dispatch = std::chrono::steady_clock::now();
                    std::string task_error;
                    const char* mode = std::getenv("TASK_SCHEDULER_EXECUTION");
                    const bool overlap_mode = false; // system table requires strict LOCKSTEP
                    const bool task_tick_ok = !task_manager.active() ||
                        (overlap_mode ? task_manager.tick_overlap(cycle, frozen, task_error) :
                                        task_manager.tick(cycle, frozen, task_error));
                    const auto barrier_complete = std::chrono::steady_clock::now();
                    if (!task_tick_ok) {
                        task_runtime_failed = true;
                        task_manager.terminate();
                        std::fprintf(stderr, "Task runtime failure: %s\n", task_error.c_str());
                        const auto previous = project_state_value;
                        set_project_state(project_state::freeze);
                        context.communications.publish_server_state(previous, project_state_value);
                    }
                    task_runtime::perf::record("server_tick", runtime_tick, perf_tick_start);
                    const auto cycle_complete = std::chrono::steady_clock::now();
                    if (task_tick_ok) {
                        const auto deadline_origin = realtime ? actual_wakeup : scheduled_tick;
                        const auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            cycle_complete - actual_wakeup).count();
                        const auto deadline_delta_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            cycle_complete - deadline_origin - tick_period).count();
                        const auto jitter_ns = realtime ? std::int64_t{0} :
                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                actual_wakeup - scheduled_tick).count();
                        task_system_table.cycle_completed(cycle,
                            static_cast<std::uint64_t>(duration_ns), deadline_delta_ns,
                            jitter_ns, frozen, task_manager.take_manual_kill());
                        if (task_system_table.stop_pending()) {
                            // Full LOCKSTEP completed; no Task is running at this boundary.
                            const auto previous = project_state_value;
                            task_manager.terminate();
                            set_project_state(project_state::stopped);
                            task_system_table.on_stop(static_cast<std::uint32_t>(project_state::stopped));
                            scheduler_dedicated_active = false;
                            scheduler_realtime_active = false;
                            context.communications.publish_server_state(previous, project_state_value);
                            break;
                        }
                    }
                    std::int64_t skipped_periods = 0;
                    if (realtime) next_tick = cycle_complete;
                    else {
                        next_tick += tick_period;
                        if (next_tick <= cycle_complete) {
                            const auto missed = (cycle_complete - next_tick) / tick_period + 1;
                            skipped_periods = static_cast<std::int64_t>(missed);
                            next_tick += tick_period * missed;
                        }
                    }
                    next_task_tick = next_tick;
                    if (deadline_file && !realtime) {
                        const auto budget_end = scheduled_tick + tick_period;
                        const auto lateness = actual_wakeup > scheduled_tick
                            ? actual_wakeup - scheduled_tick : std::chrono::steady_clock::duration::zero();
                        const auto overrun = cycle_complete > budget_end
                            ? cycle_complete - budget_end : std::chrono::steady_clock::duration::zero();
                        const auto us = [](std::chrono::steady_clock::duration value) {
                            return std::chrono::duration<double, std::micro>(value).count();
                        };
                        std::fprintf(deadline_file,
                            "%llu,%lld,%lld,%lld,%lld,%lld,%.3f,%.3f,%.3f,%d,%.3f,%lld\n",
                            static_cast<unsigned long long>(runtime_tick),
                            static_cast<long long>(deadline_ns(scheduled_tick)),
                            static_cast<long long>(deadline_ns(actual_wakeup)),
                            static_cast<long long>(deadline_ns(task_dispatch)),
                            static_cast<long long>(deadline_ns(barrier_complete)),
                            static_cast<long long>(deadline_ns(cycle_complete)),
                            us(lateness), us(barrier_complete - task_dispatch),
                            us(cycle_complete - actual_wakeup), cycle_complete > budget_end ? 1 : 0,
                            us(overrun), static_cast<long long>(skipped_periods));
                    }
                    if (!task_tick_ok) break;
                }
            }
            if (stop_event) CloseHandle(stop_event);
        });
    }
    while (running) {
        const auto project_period = std::chrono::microseconds(scheduler_project_period_us);
        const auto now =
            std::chrono::system_clock::now();

        if (!context.lease.valid_at(now)) {
            running = false;
            break;
        }

        communication_control_message message;

        const bool ticking = !scheduler_dedicated_active && context.project && !task_runtime_failed &&
            (project_state_value == project_state::run ||
             project_state_value == project_state::freeze);
        // Convert the steady-clock deadline to the queue's system-clock domain.
        // Re-evaluate each iteration so wall-clock adjustments cannot accumulate.
        const auto steady_now = std::chrono::steady_clock::now();
        const auto remaining = next_task_tick > steady_now
            ? next_task_tick - steady_now
            : std::chrono::steady_clock::duration::zero();
        const auto deadline = ticking
            ? (std::min)(context.lease.expires_at(),
                std::chrono::system_clock::now() +
                    std::chrono::duration_cast<std::chrono::system_clock::duration>(remaining))
            : context.lease.expires_at();
        bool received = false;
        if (ticking && hr_enabled && queue_event) {
            const auto target = hybrid ? next_task_tick - spin_budget : next_task_tick;
            const bool timer_fired = hr_timer.wait(queue_event, target);
            if (timer_fired && hybrid) {
                // Keep TCP responsive during the final spin by checking the queue event.
                while (std::chrono::steady_clock::now() < next_task_tick) {
                    if (WaitForSingleObject(queue_event, 0) == WAIT_OBJECT_0) break;
                    YieldProcessor();
                }
            }
            if (WaitForSingleObject(queue_event, 0) == WAIT_OBJECT_0) {
                received = context.requests.wait_pop_until(
                    std::chrono::system_clock::now(), message);
            }
        } else {
            received = context.requests.wait_pop_until(deadline, message);
        }
        if (!received) {
            if (ticking && context.lease.valid_at(std::chrono::system_clock::now())) {
                const auto now_tick = std::chrono::steady_clock::now();
                if (now_tick >= next_task_tick) {
                    const auto scheduled_tick = next_task_tick;
                    const auto actual_wakeup = now_tick;
                    const auto perf_tick_start = task_runtime::perf::clock::now();
                    static auto perf_previous_tick = perf_tick_start;
                    task_runtime::perf::record("cycle_interval", runtime_tick, perf_previous_tick);
                    perf_previous_tick = perf_tick_start;
                    ++runtime_tick;
                    const bool frozen = project_state_value == project_state::freeze;
                    if (!frozen) ++model_cycle;
                    std::string task_error;
                    // Runtime tick advances in FREEZE; model cycle remains stable.
                    const auto cycle = frozen ? runtime_tick : model_cycle;
                    const auto task_dispatch = std::chrono::steady_clock::now();
                    const bool task_tick_ok = !task_manager.active() ||
                        (overlap ? task_manager.tick_overlap(cycle, frozen, task_error) :
                            task_manager.tick(cycle, frozen, task_error));
                    const auto barrier_complete = std::chrono::steady_clock::now();
                    if (!task_tick_ok) {
                        task_runtime_failed = true;
                        task_manager.terminate();
                        std::fprintf(stderr, "Task runtime failure: %s\n", task_error.c_str());
                        const auto previous = project_state_value;
                        set_project_state(project_state::freeze);
                        context.communications.publish_server_state(previous, project_state_value);
                    }
                    task_runtime::perf::record("server_tick", runtime_tick, perf_tick_start);
                    const auto cycle_complete = std::chrono::steady_clock::now();
                    if (task_tick_ok) {
                        const auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            cycle_complete - actual_wakeup).count();
                        const auto deadline_delta_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            cycle_complete - scheduled_tick - project_period).count();
                        const auto jitter_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            actual_wakeup - scheduled_tick).count();
                        task_system_table.cycle_completed(cycle,
                            static_cast<std::uint64_t>(duration_ns), deadline_delta_ns,
                            jitter_ns, frozen, task_manager.take_manual_kill());
                        if (task_system_table.stop_pending()) {
                            const auto previous = project_state_value;
                            task_manager.terminate();
                            set_project_state(project_state::stopped);
                            task_system_table.on_stop(static_cast<std::uint32_t>(project_state::stopped));
                            scheduler_dedicated_active = false;
                            scheduler_realtime_active = false;
                            context.communications.publish_server_state(previous, project_state_value);
                        }
                    }
                    std::int64_t skipped_periods = 0;
                    if (precision_timer.enabled() || hr_enabled) {
                        // Absolute cadence; skip missed deadlines without catch-up bursts.
                        const auto completed = std::chrono::steady_clock::now();
                        next_task_tick += project_period;
                        if (next_task_tick <= completed) {
                            const auto missed = (completed - next_task_tick) /
                                project_period + 1;
                            skipped_periods = static_cast<std::int64_t>(missed);
                            next_task_tick += project_period * missed;
                        }
                    } else {
                        next_task_tick = std::chrono::steady_clock::now() +
                            project_period;
                    }
                    if (deadline_file) {
                        const auto budget_end = scheduled_tick + project_period;
                        const auto lateness = actual_wakeup > scheduled_tick
                            ? actual_wakeup - scheduled_tick
                            : std::chrono::steady_clock::duration::zero();
                        const auto overrun = cycle_complete > budget_end
                            ? cycle_complete - budget_end
                            : std::chrono::steady_clock::duration::zero();
                        const auto us = [](std::chrono::steady_clock::duration value) {
                            return std::chrono::duration<double, std::micro>(value).count();
                        };
                        std::fprintf(deadline_file,
                            "%llu,%lld,%lld,%lld,%lld,%lld,%.3f,%.3f,%.3f,%d,%.3f,%lld\n",
                            static_cast<unsigned long long>(runtime_tick),
                            static_cast<long long>(deadline_ns(scheduled_tick)),
                            static_cast<long long>(deadline_ns(actual_wakeup)),
                            static_cast<long long>(deadline_ns(task_dispatch)),
                            static_cast<long long>(deadline_ns(barrier_complete)),
                            static_cast<long long>(deadline_ns(cycle_complete)),
                            us(lateness), us(barrier_complete - task_dispatch),
                            us(cycle_complete - actual_wakeup),
                            cycle_complete > budget_end ? 1 : 0,
                            us(overrun), static_cast<long long>(skipped_periods));
                    }
                }
                continue;
            }
            running = false;
            break;
        }

        if (!context.lease.valid_at(
                std::chrono::system_clock::now())) {

            running = false;
            break;
        }

        if (auto* request =
                std::get_if<server_request_message>(&message)) {

            const auto old_state =
                current_project_state();

            auto result =
                execute_message(*request);

            if (request->request.kind ==
                server_request_kind::shutdown) {

                request->origin.arm_written();
            }

            request->origin.present(result);

            if (request->request.kind ==
                server_request_kind::shutdown) {

                static_cast<void>(
                    request->origin.wait_written(
                        shutdown_ack_timeout));
            }

            const auto new_state =
                current_project_state();

            context.communications.publish_server_state(
                old_state,
                new_state);
        } else if (auto* login =
                       std::get_if<login_control_message>(&message)) {

            execute_login(*login);
        } else if (auto* close =
                       std::get_if<connection_close_control_message>(&message)) {

            execute_connection_close(*close);
        } else if (auto* client =
                       std::get_if<client_control_message>(&message)) {

            execute_client(*client);
        }
    }

    // Join before closing the diagnostics file or destroying Project/SHM.
    {
        std::lock_guard<std::mutex> lock(scheduler_mutex);
        scheduler_exit = true;
    }
    scheduler_cv.notify_all();
    if (scheduler_thread.joinable()) scheduler_thread.join();
    if (deadline_file) std::fclose(deadline_file);
    // SHUTDOWN result has already been presented to its originating endpoint.
    shutdown();

    return 0;
}

project_state server::current_project_state() const noexcept {
    return project_state_value;
}

void server::update_system_datetime() noexcept {

    if (!context.project) {
        return;
    }

    const auto now =
        std::chrono::system_clock::now().
            time_since_epoch();

    context.project->system().
        server_datetime_ns =
            std::chrono::duration_cast<
                std::chrono::nanoseconds>(
                    now).count();
}

void server::set_project_state(
    project_state state) noexcept {

    project_state_value =
        state;
    task_system_table.on_project_state(static_cast<std::uint32_t>(state));

    if (context.project) {
        context.project->system().state =
            static_cast<std::uint32_t>(
                state);

        update_system_datetime();
    }
}

void server::execute_login(
    login_control_message& message) {

    if (message.connection == nullptr) {
        return;
    }

    server_response result;
    result.operation = next_operation();

    if (!context.communications.login_connection(
            *message.connection, message.name)) {
        // Failed login is terminal; no success state or subsequent commands.
        return;
    }

    result.payload =
        server_response_payload_kind::state;

    result.state.project =
        current_project_state();

    result.status =
        server_status::success;

    try {
        (void)message.connection->enqueue_response(message.request, result);
    } catch (...) {
        (void)message.connection->begin_close();
        context.communications.unregister_connection(message.connection->id());
    }
}

void server::execute_connection_close(
    connection_close_control_message& message) noexcept {

    if (message.connection == nullptr) {
        return;
    }

    (void)message.connection->begin_close();

    context.communications.unregister_connection(
        message.connection->id());
}

void server::execute_client(
    client_control_message& message) {

    if (message.connection == nullptr) {
        return;
    }

    server_response result;
    result.operation = next_operation();

    if (!message.connection->logged_in() ||
        message.connection->closing()) {

        result.status =
            server_status::communication_invalid_request;
    } else {
        (void)context.communications.route_client(
            *message.connection,
            message.message);

        result.status =
            server_status::success;
    }

    (void)message.connection->enqueue_response(
        message.request,
        result);
}

server_response server::execute_message(
    const server_request_message& message) {

    if (message.connection != nullptr &&
        (!message.connection->logged_in() || message.connection->closing())) {
        server_response rejected;
        rejected.operation = next_operation();
        rejected.status = server_status::communication_invalid_request;
        return rejected;
    }

    if (context.policy.allows(
            context.mode,
            message.request.kind)) {

        return execute(
            message.request);
    }

    server_response result;
    result.operation =
        next_operation();
    result.status =
        server_status::access_denied;

    result.diagnostics.emit(
        diagnostic(
            diagnostics::server_policy_denied,
            result.operation)
            .detail(
                "Request is not allowed for this origin/client identity")
            .build());

    result.diagnostics.sort_deterministic();
    return result;
}

server_response server::execute(
    const server_request& request) {

    server_response result;
    result.operation =
        next_operation();

    update_system_datetime();

    switch (request.kind) {
    case server_request_kind::load:
        result.status =
            load(
                request.path,
                result.operation,
                result.diagnostics);
        break;

    case server_request_kind::publish:
        result.status =
            publish(
                request.path,
                result.operation,
                result.diagnostics);
        break;

    case server_request_kind::build:
        result.status =
            build(
                request.path,
                result.operation,
                result.diagnostics);
        break;

    case server_request_kind::unload:
        result.status =
            unload(
                result.operation,
                result.diagnostics);
        break;

    case server_request_kind::rebuild:
        result.status =
            rebuild(
                request.path,
                result.operation,
                result.diagnostics);
        break;

    case server_request_kind::get_state:
        result.payload =
            server_response_payload_kind::state;
        result.state.project =
            current_project_state();
        result.status =
            server_status::success;
        break;

    case server_request_kind::snap_ic:
        result.status =
            snap_ic(
                request.path,
                request.snap_options,
                result.operation,
                result.diagnostics);
        break;

    case server_request_kind::reset_ic:
        result.status =
            reset_ic(
                request.path,
                request.reset_options,
                result.operation,
                result.diagnostics);
        break;

    case server_request_kind::delete_ic:
        result.status =
            delete_ic(
                request.path,
                result.operation,
                result.diagnostics);
        break;

    case server_request_kind::list_ic:
        result.status =
            list_ic(
                result.catalog,
                result.operation,
                result.diagnostics);

        if (succeeded(result.status)) {
            result.payload =
                server_response_payload_kind::ic_catalog;
        }
        break;

    case server_request_kind::run:
        result.status =
            run_project(
                result.operation,
                result.diagnostics);
        break;

    case server_request_kind::freeze:
        result.status =
            freeze_project(
                result.operation,
                result.diagnostics);
        break;

    case server_request_kind::kill_task:
        result.status = kill_task(request.name, result.operation, result.diagnostics);
        break;

    case server_request_kind::start_task:
        result.status = start_task(request.name, result.operation, result.diagnostics);
        break;

    case server_request_kind::get_object: {
        if (!context.project) {
            result.diagnostics.emit(
                diagnostic(
                    diagnostics::project_not_loaded,
                    result.operation)
                    .detail(
                        "GET_OBJECT requires an active Project")
                    .build());

            result.status =
                server_status::project_not_loaded;
            break;
        }

        const auto queried =
            get_runtime_object(
                context.project->compiled(),
                request.name,
                request.type,
                result.object);

        switch (queried) {
        case runtime_query_result::success:
            result.payload =
                server_response_payload_kind::
                    runtime_object;
            result.status =
                server_status::success;
            break;

        case runtime_query_result::invalid_input:
            result.diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_query_invalid,
                    result.operation)
                    .detail(request.name)
                    .build());
            result.status =
                server_status::runtime_query_invalid;
            break;

        case runtime_query_result::not_found:
            result.diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_query_not_found,
                    result.operation)
                    .detail(request.name)
                    .build());
            result.status =
                server_status::runtime_query_not_found;
            break;

        case runtime_query_result::unsupported_type:
            result.diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_query_unsupported,
                    result.operation)
                    .detail(request.name)
                    .build());
            result.status =
                server_status::unsupported;
            break;

        case runtime_query_result::invalid_runtime:
            result.diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_query_failed,
                    result.operation)
                    .detail(request.name)
                    .build());
            result.status =
                server_status::project_runtime_failed;
            break;
        }

        break;
    }

    case server_request_kind::get_type: {
        if (!context.project) {
            result.diagnostics.emit(
                diagnostic(
                    diagnostics::project_not_loaded,
                    result.operation)
                    .detail(
                        "GET_TYPE requires an active Project")
                    .build());

            result.status =
                server_status::project_not_loaded;
            break;
        }

        const auto queried =
            get_runtime_type(
                context.project->compiled(),
                request.objects,
                result.type);

        switch (queried) {
        case runtime_query_result::success:
            result.payload =
                server_response_payload_kind::
                    runtime_type;
            result.status =
                server_status::success;
            break;

        case runtime_query_result::invalid_input:
            result.diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_query_invalid,
                    result.operation)
                    .detail("GET_TYPE")
                    .build());
            result.status =
                server_status::runtime_query_invalid;
            break;

        case runtime_query_result::not_found:
            result.diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_query_not_found,
                    result.operation)
                    .detail("GET_TYPE")
                    .build());
            result.status =
                server_status::runtime_query_not_found;
            break;

        case runtime_query_result::unsupported_type:
            result.diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_query_unsupported,
                    result.operation)
                    .detail("GET_TYPE")
                    .build());
            result.status =
                server_status::unsupported;
            break;

        case runtime_query_result::invalid_runtime:
            result.diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_query_failed,
                    result.operation)
                    .detail("GET_TYPE")
                    .build());
            result.status =
                server_status::project_runtime_failed;
            break;
        }

        break;
    }

    case server_request_kind::get_link: {
        if (!context.project) {
            result.diagnostics.emit(
                diagnostic(
                    diagnostics::project_not_loaded,
                    result.operation)
                    .detail(
                        "GET_LINK requires an active Project")
                    .build());

            result.status =
                server_status::project_not_loaded;
            break;
        }

        const auto queried =
            get_runtime_link(
                context.project->compiled(),
                request.objects,
                result.link);

        switch (queried) {
        case runtime_query_result::success:
            result.payload =
                server_response_payload_kind::
                    runtime_link;
            result.status =
                server_status::success;
            break;

        case runtime_query_result::invalid_input:
            result.diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_query_invalid,
                    result.operation)
                    .detail("GET_LINK")
                    .build());
            result.status =
                server_status::runtime_query_invalid;
            break;

        case runtime_query_result::not_found:
            result.diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_query_not_found,
                    result.operation)
                    .detail("GET_LINK")
                    .build());
            result.status =
                server_status::runtime_query_not_found;
            break;

        case runtime_query_result::unsupported_type:
            result.diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_query_unsupported,
                    result.operation)
                    .detail("GET_LINK")
                    .build());
            result.status =
                server_status::unsupported;
            break;

        case runtime_query_result::invalid_runtime:
            result.diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_query_failed,
                    result.operation)
                    .detail("GET_LINK")
                    .build());
            result.status =
                server_status::project_runtime_failed;
            break;
        }

        break;
    }

    case server_request_kind::get_value: {
        if (!context.project) {
            result.diagnostics.emit(
                diagnostic(
                    diagnostics::project_not_loaded,
                    result.operation)
                    .detail(
                        "GET_VALUE requires an active Project")
                    .build());

            result.status =
                server_status::project_not_loaded;
            break;
        }

        const auto bytes =
            context.project->shm().bytes();

        const auto logical_size =
            static_cast<std::size_t>(
                context.project->runtime_size());

        if (logical_size > bytes.size()) {
            result.diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_query_failed,
                    result.operation)
                    .detail(request.name)
                    .build());

            result.status =
                server_status::project_runtime_failed;
            break;
        }

        runtime_value value;

        const auto queried =
            get_runtime_value(
                context.project->compiled(),
                context.project->runtime_bindings(),
                bytes.first(logical_size),
                request.name,
                value);

        switch (queried) {
        case runtime_query_result::success:
            result.payload =
                server_response_payload_kind::runtime_value;
            result.value = value;
            result.status =
                server_status::success;
            break;

        case runtime_query_result::invalid_input:
            result.diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_query_invalid,
                    result.operation)
                    .detail(request.name)
                    .build());
            result.status =
                server_status::runtime_query_invalid;
            break;

        case runtime_query_result::not_found:
            result.diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_query_not_found,
                    result.operation)
                    .detail(request.name)
                    .build());
            result.status =
                server_status::runtime_query_not_found;
            break;

        case runtime_query_result::unsupported_type:
            result.diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_query_unsupported,
                    result.operation)
                    .detail(request.name)
                    .build());
            result.status =
                server_status::unsupported;
            break;

        case runtime_query_result::invalid_runtime:
            result.diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_query_failed,
                    result.operation)
                    .detail(request.name)
                    .build());
            result.status =
                server_status::project_runtime_failed;
            break;
        }

        break;
    }

    case server_request_kind::shutdown:
        // Keep the originating endpoint alive until run() presents this result.
        running = false;
        result.status =
            server_status::success;
        break;
    }

    result.diagnostics.sort_deterministic();
    return result;
}

server_status server::load(
    const std::filesystem::path& project_path,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (context.project) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_already_loaded,
                operation)
                .detail("UNLOAD the active Project before LOAD")
                .build());

        return server_status::project_already_loaded;
    }

    const auto path =
        resolve(
            context.configuration_directory,
            project_path);

    std::unique_ptr<project> candidate;

    const auto status =
        load_project(
            path,
            context.configuration.settings,
            operation,
            diagnostics,
            candidate);

    if (!succeeded(status)) {
        context.project.reset();
        set_project_state(project_state::unloaded);
        return status;
    }

    const auto configured =
        load_resident_project_configuration(
            *candidate,
            operation,
            diagnostics);

    if (!succeeded(configured)) {
        candidate.reset();
        context.project.reset();
        set_project_state(project_state::unloaded);
        return configured;
    }

    context.project =
        std::move(candidate);

    std::string table_error;
    if (!task_system_table.create(context.project->tasks(), table_error)) {
        diagnostics.emit(diagnostic(diagnostics::runtime_state_invalid, operation)
            .detail("System Task SHM initialization failed: " + table_error).build());
        context.project.reset();
        set_project_state(project_state::unloaded);
        return server_status::project_runtime_failed;
    }
    task_manager.set_system_table(&task_system_table);
    set_project_state(project_state::loaded);

    return server_status::success;
}

server_status server::publish(
    const std::filesystem::path& project_path,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (context.project) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_already_loaded,
                operation)
                .detail("UNLOAD the active Project before PUBLISH")
                .build());

        return server_status::project_already_loaded;
    }

    const auto path =
        resolve(
            context.configuration_directory,
            project_path);

    std::unique_ptr<project> candidate;

    const auto status =
        publish_project(
            path,
            context.configuration.settings,
            operation,
            diagnostics,
            candidate);

    if (!succeeded(status)) {
        context.project.reset();
        set_project_state(project_state::unloaded);
        return status;
    }

    const auto configured =
        load_resident_project_configuration(
            *candidate,
            operation,
            diagnostics);

    if (!succeeded(configured)) {
        candidate.reset();
        context.project.reset();
        set_project_state(project_state::unloaded);
        return configured;
    }

    context.project =
        std::move(candidate);

    std::string table_error;
    if (!task_system_table.create(context.project->tasks(), table_error)) {
        diagnostics.emit(diagnostic(diagnostics::runtime_state_invalid, operation)
            .detail("System Task SHM initialization failed: " + table_error).build());
        context.project.reset();
        set_project_state(project_state::unloaded);
        return server_status::project_runtime_failed;
    }
    task_manager.set_system_table(&task_system_table);
    set_project_state(project_state::loaded);

    return server_status::success;
}

server_status server::build(
    const std::filesystem::path& project_path,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (context.project) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_already_loaded,
                operation)
                .detail("UNLOAD the active Project before BUILD")
                .build());

        return server_status::project_already_loaded;
    }

    const auto path =
        resolve(
            context.configuration_directory,
            project_path);

    std::unique_ptr<project> candidate;

    const auto status =
        build_project(
            path,
            context.configuration.settings,
            operation,
            diagnostics,
            candidate);

    if (!succeeded(status)) {
        context.project.reset();
        set_project_state(project_state::unloaded);
        return status;
    }

    const auto configured =
        load_resident_project_configuration(
            *candidate,
            operation,
            diagnostics);

    if (!succeeded(configured)) {
        candidate.reset();
        context.project.reset();
        set_project_state(project_state::unloaded);
        return configured;
    }

    context.project =
        std::move(candidate);

    std::string table_error;
    if (!task_system_table.create(context.project->tasks(), table_error)) {
        diagnostics.emit(diagnostic(diagnostics::runtime_state_invalid, operation)
            .detail("System Task SHM initialization failed: " + table_error).build());
        context.project.reset();
        set_project_state(project_state::unloaded);
        return server_status::project_runtime_failed;
    }
    task_manager.set_system_table(&task_system_table);
    set_project_state(project_state::loaded);

    return server_status::success;
}

server_status server::rebuild(
    const std::filesystem::path& project_path,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (context.project) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_already_loaded,
                operation)
                .detail("UNLOAD the active Project before REBUILD")
                .build());

        return server_status::project_already_loaded;
    }

    const auto path =
        resolve(
            context.configuration_directory,
            project_path);

    std::unique_ptr<project> candidate;

    const auto status =
        rebuild_project(
            path,
            context.configuration.settings,
            operation,
            diagnostics,
            candidate);

    if (!succeeded(status)) {
        context.project.reset();
        set_project_state(project_state::unloaded);
        return status;
    }

    const auto configured =
        load_resident_project_configuration(
            *candidate,
            operation,
            diagnostics);

    if (!succeeded(configured)) {
        candidate.reset();
        context.project.reset();
        set_project_state(project_state::unloaded);
        return configured;
    }

    context.project =
        std::move(candidate);

    std::string table_error;
    if (!task_system_table.create(context.project->tasks(), table_error)) {
        diagnostics.emit(diagnostic(diagnostics::runtime_state_invalid, operation)
            .detail("System Task SHM initialization failed: " + table_error).build());
        context.project.reset();
        set_project_state(project_state::unloaded);
        return server_status::project_runtime_failed;
    }
    task_manager.set_system_table(&task_system_table);
    set_project_state(project_state::loaded);

    return server_status::success;
}

server_status server::snap_ic(
    const std::filesystem::path& ic_path,
    snap_ic_options options,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (options != snap_ic_options::none ||
        ic_path.empty()) {

        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_invalid,
                operation)
                .detail(
                    "SNAP_IC requires path and valid options")
                .build());

        return server_status::runtime_ic_invalid;
    }

    if (!context.project) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_not_loaded,
                operation)
                .detail("SNAP_IC requires an active Project")
                .build());

        return server_status::project_not_loaded;
    }

    if (!project_state_can_ic(project_state_value)) {
        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_state_invalid,
                operation)
                .detail("SNAP_IC requires LOADED or FREEZE")
                .build());

        return server_status::runtime_state_invalid;
    }

    if (!succeeded(require_ic_catalog(
            *context.project,
            operation,
            diagnostics))) {
        return server_status::runtime_ic_invalid;
    }

    const auto path = resolve(
        context.project->ic_catalog_path().parent_path(),
        ic_path);

    ic_catalog catalog_candidate;

    try {
        catalog_candidate =
            context.project->
                ic_catalog_data();
    }
    catch (...) {
        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_failed,
                operation)
                .detail(
                    "Cannot allocate SNAP_IC catalog candidate")
                .build());

        return server_status::runtime_ic_failed;
    }

    if (!upsert_ic_catalog_entry(
            catalog_candidate,
            context.project->ic_catalog_path(),
            ic_path,
            0)) {

        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_invalid,
                operation)
                .detail(
                    "SNAP_IC path is invalid for IC.json catalog")
                .build());

        return server_status::runtime_ic_invalid;
    }

    const auto previous_state = project_state_value;
    set_project_state(project_state::snapping_ic);
    context.communications.publish_server_state(
        previous_state,
        project_state_value);

    const auto status = [&]() -> server_status {
        const auto runtime =
            static_cast<const project&>(*context.project).shm().bytes();
        const auto logical_size = context.project->runtime_size();

        if (logical_size == 0 ||
            logical_size > static_cast<std::uint64_t>(runtime.size())) {

            diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_ic_failed,
                    operation)
                    .detail("Runtime SHM size is invalid")
                    .build());
            return server_status::runtime_ic_failed;
        }

        std::vector<std::byte> image;
        if (snapshot_runtime_ic_binary(
                context.project->compiled(),
                context.project->runtime_bindings(),
                runtime.first(static_cast<std::size_t>(logical_size)),
                image) != runtime_ic_snapshot_result::success) {

            diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_ic_failed,
                    operation)
                    .detail("SNAP_IC could not capture Runtime")
                    .build());
            return server_status::runtime_ic_failed;
        }

        auto temporary_path =
            path;

        temporary_path += ".new";

        auto backup_path =
            path;

        backup_path += ".snap-backup";

        // Temporary/rollback files must never alias another configured IC.
        // IC identity remains the resolved snapshot path; these are only
        // persistence artifacts of this SNAP operation.
        if (find_ic_catalog_entry(
                context.project->ic_catalog_data(),
                context.project->ic_catalog_path(),
                temporary_path) != nullptr ||
            find_ic_catalog_entry(
                context.project->ic_catalog_data(),
                context.project->ic_catalog_path(),
                backup_path) != nullptr) {

            diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_ic_invalid,
                    operation)
                    .detail(
                        "SNAP_IC temporary path conflicts with another IC")
                    .build());

            return server_status::runtime_ic_invalid;
        }

        std::error_code file_error;

        const auto target_exists =
            std::filesystem::exists(
                path,
                file_error);

        if (file_error) {
            diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_ic_io_failed,
                    operation)
                    .detail(
                        path.generic_string())
                    .build());

            return server_status::io_error;
        }

        if (target_exists) {
            if (!std::filesystem::is_regular_file(
                    path,
                    file_error) ||
                file_error) {

                diagnostics.emit(
                    diagnostic(
                        diagnostics::runtime_ic_io_failed,
                        operation)
                        .detail(
                            path.generic_string())
                        .build());

                return server_status::io_error;
            }
        }

        file_error.clear();

        std::filesystem::remove(
            temporary_path,
            file_error);

        if (file_error) {
            diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_ic_io_failed,
                    operation)
                    .detail(
                        temporary_path.generic_string())
                    .build());

            return server_status::io_error;
        }

        const auto backup_exists =
            std::filesystem::exists(
                backup_path,
                file_error);

        if (file_error ||
            backup_exists) {

            diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_ic_io_failed,
                    operation)
                    .detail(
                        backup_path.generic_string())
                    .build());

            return server_status::io_error;
        }

        bool backup_active = false;

        if (target_exists) {
            std::filesystem::copy_file(
                path,
                backup_path,
                std::filesystem::copy_options::none,
                file_error);

            if (file_error) {
                diagnostics.emit(
                    diagnostic(
                        diagnostics::runtime_ic_io_failed,
                        operation)
                        .detail(
                            backup_path.generic_string())
                        .build());

                return server_status::io_error;
            }

            backup_active = true;
        }

        const auto cleanup_temporary =
            [&]() noexcept {

                std::error_code ignored;

                std::filesystem::remove(
                    temporary_path,
                    ignored);
            };

        const auto cleanup_backup =
            [&]() noexcept {

                if (!backup_active) {
                    return;
                }

                std::error_code ignored;

                std::filesystem::remove(
                    backup_path,
                    ignored);
            };

        bool target_replaced = false;

        const auto rollback_target =
            [&]() noexcept {

                cleanup_temporary();

                if (!target_replaced) {
                    cleanup_backup();
                    return true;
                }

                if (!backup_active) {
                    std::error_code rollback_error;

                    std::filesystem::remove(
                        path,
                        rollback_error);

                    return !rollback_error;
                }

                if (replace_file(
                        backup_path,
                        path) !=
                    filesystem_replace_result::success) {

                    return false;
                }

                backup_active = false;
                return true;
            };

        writable_file_mapping mapping;

        if (mapping.create(
                temporary_path,
                image.size()) !=
            writable_file_mapping_result::success) {

            cleanup_temporary();
            cleanup_backup();

            diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_ic_io_failed,
                    operation)
                    .detail(
                        temporary_path.generic_string())
                    .build());

            return server_status::io_error;
        }

        std::memcpy(
            mapping.bytes().data(),
            image.data(),
            image.size());

        if (mapping.flush() !=
            writable_file_mapping_result::success) {

            mapping.reset();

            cleanup_temporary();
            cleanup_backup();

            diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_ic_io_failed,
                    operation)
                    .detail(
                        temporary_path.generic_string())
                    .build());

            return server_status::io_error;
        }

        mapping.reset();

        // Visibility boundary: the complete flushed .new image replaces the
        // old snapshot atomically. Readers never observe a partial target.
        if (replace_file(
                temporary_path,
                path) !=
            filesystem_replace_result::success) {

            cleanup_temporary();
            cleanup_backup();

            diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_ic_io_failed,
                    operation)
                    .detail(
                        path.generic_string())
                    .build());

            return server_status::io_error;
        }

        target_replaced = true;

        if (!upsert_ic_catalog_entry(
                catalog_candidate,
                context.project->ic_catalog_path(),
                ic_path,
                static_cast<std::uint64_t>(
                    image.size()))) {

            if (!rollback_target()) {
                diagnostics.emit(
                    diagnostic(
                        diagnostics::runtime_ic_io_failed,
                        operation)
                        .detail(
                            "SNAP_IC failed to restore the previous snapshot file")
                        .build());
            }

            diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_ic_invalid,
                    operation)
                    .detail(
                        "SNAP_IC catalog candidate became invalid")
                    .build());

            return server_status::runtime_ic_invalid;
        }

        if (save_ic_catalog(
                context.project->ic_catalog_path(),
                catalog_candidate) !=
            ic_catalog_result::success) {

            if (!rollback_target()) {
                diagnostics.emit(
                    diagnostic(
                        diagnostics::runtime_ic_io_failed,
                        operation)
                        .detail(
                            "SNAP_IC failed to restore the previous snapshot file")
                        .build());
            }

            diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_ic_io_failed,
                    operation)
                    .detail(
                        context.project->
                            ic_catalog_path().
                            generic_string())
                    .build());

            return server_status::io_error;
        }

        context.project->set_ic_catalog(
            std::move(
                catalog_candidate));

        ++context.project->system().
            snapshot_generation;

        // Catalog is now authoritative. Backup cleanup is no longer part
        // of the logical SNAP transaction.
        cleanup_backup();

        return server_status::success;
    }();

    set_project_state(previous_state);
    context.communications.publish_server_state(
        project_state::snapping_ic,
        previous_state);

    return status;
}

server_status server::reset_ic(
    const std::filesystem::path& ic_path,
    reset_ic_options options,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (!valid_reset_ic_options(options) ||
        ic_path.empty()) {

        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_invalid,
                operation)
                .detail(
                    "RESET_IC requires path and valid options")
                .build());
        return server_status::runtime_ic_invalid;
    }

    if (!context.project) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_not_loaded,
                operation)
                .detail("RESET_IC requires an active Project")
                .build());
        return server_status::project_not_loaded;
    }

    if (!project_state_can_ic(project_state_value)) {
        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_state_invalid,
                operation)
                .detail("RESET_IC requires LOADED or FREEZE")
                .build());
        return server_status::runtime_state_invalid;
    }

    if (!succeeded(require_ic_catalog(
            *context.project,
            operation,
            diagnostics))) {
        return server_status::runtime_ic_invalid;
    }

    const auto* node =
        find_ic_catalog_entry(
            context.project->ic_catalog_data(),
            context.project->ic_catalog_path(),
            ic_path);

    if (node == nullptr) {
        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_not_found,
                operation)
                .detail(ic_path.generic_string())
                .build());

        return server_status::runtime_ic_not_found;
    }

    const auto path = resolve(
        context.project->ic_catalog_path().parent_path(),
        node->path);

    const auto previous_state = project_state_value;
    set_project_state(project_state::resetting_ic);
    context.communications.publish_server_state(
        previous_state,
        project_state_value);

    const auto status = [&]() -> server_status {
        read_only_file_mapping mapping;
        const auto opened = mapping.open(path);

        if (opened != read_only_file_mapping_result::success) {
            const bool missing =
                opened == read_only_file_mapping_result::not_found;

            diagnostics.emit(
                diagnostic(
                    missing
                        ? diagnostics::runtime_ic_not_found
                        : diagnostics::runtime_ic_io_failed,
                    operation)
                    .detail(path.generic_string())
                    .build());

            return missing
                ? server_status::runtime_ic_not_found
                : server_status::io_error;
        }

        runtime_ic_binary_view image;
        if (image.bind(mapping.bytes()) !=
            runtime_ic_codec_result::success) {

            diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_ic_invalid,
                    operation)
                    .detail(path.generic_string())
                    .build());
            return server_status::runtime_ic_invalid;
        }

        auto runtime = context.project->shm().bytes();
        const auto logical_size = context.project->runtime_size();

        if (logical_size == 0 ||
            logical_size > static_cast<std::uint64_t>(runtime.size())) {

            diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_ic_failed,
                    operation)
                    .detail("Runtime SHM size is invalid")
                    .build());
            return server_status::runtime_ic_failed;
        }

        runtime_ic_reset_stats reset_stats;

        const auto reset = reset_runtime_ic_binary(
            context.project->compiled(),
            context.project->runtime_bindings(),
            runtime.first(static_cast<std::size_t>(logical_size)),
            image,
            &reset_stats,
            options);

        switch (reset) {
        case runtime_ic_reset_result::success:
            if (reset_stats.records != 0) {
                ++context.project->system().
                    value_generation;
            }

            ++context.project->system().
                current_ic_generation;

            return server_status::success;
        case runtime_ic_reset_result::invalid_input:
        case runtime_ic_reset_result::invalid_image:
            diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_ic_invalid,
                    operation)
                    .detail(path.generic_string())
                    .build());
            return server_status::runtime_ic_invalid;
        case runtime_ic_reset_result::not_found:
            diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_ic_not_found,
                    operation)
                    .detail(path.generic_string())
                    .build());
            return server_status::runtime_ic_not_found;
        case runtime_ic_reset_result::unsupported_type:
            diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_ic_failed,
                    operation)
                    .detail(
                        "RESET_IC encountered an unsupported Runtime type")
                    .build());
            return server_status::unsupported;
        case runtime_ic_reset_result::type_mismatch:
            diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_ic_type_mismatch,
                    operation)
                    .detail(path.generic_string())
                    .build());
            return server_status::runtime_ic_type_mismatch;
        case runtime_ic_reset_result::invalid_runtime:
        case runtime_ic_reset_result::failed:
            diagnostics.emit(
                diagnostic(
                    diagnostics::runtime_ic_failed,
                    operation)
                    .detail(path.generic_string())
                    .build());
            return server_status::runtime_ic_failed;
        }

        return server_status::runtime_ic_failed;
    }();

    set_project_state(previous_state);
    context.communications.publish_server_state(
        project_state::resetting_ic,
        previous_state);

    return status;
}

server_status server::delete_ic(
    const std::filesystem::path& ic_path,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (ic_path.empty()) {
        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_invalid,
                operation)
                .detail("DELETE_IC requires path")
                .build());
        return server_status::runtime_ic_invalid;
    }

    if (!context.project) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_not_loaded,
                operation)
                .detail("DELETE_IC requires an active Project")
                .build());
        return server_status::project_not_loaded;
    }

    if (!succeeded(require_ic_catalog(
            *context.project,
            operation,
            diagnostics))) {
        return server_status::runtime_ic_invalid;
    }

    ic_catalog catalog;

    try {
        catalog =
            context.project->ic_catalog_data();
    }
    catch (...) {
        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_failed,
                operation)
                .detail(
                    "Cannot allocate DELETE_IC catalog candidate")
                .build());

        return server_status::runtime_ic_failed;
    }

    std::filesystem::path removed_path;
    if (!erase_ic_catalog_entry(
            catalog,
            context.project->ic_catalog_path(),
            ic_path,
            removed_path)) {

        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_not_found,
                operation)
                .detail(ic_path.generic_string())
                .build());
        return server_status::runtime_ic_not_found;
    }

    if (save_ic_catalog(
            context.project->ic_catalog_path(),
            catalog) != ic_catalog_result::success) {

        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_io_failed,
                operation)
                .detail(
                    context.project->ic_catalog_path().generic_string())
                .build());
        return server_status::io_error;
    }

    context.project->set_ic_catalog(
        std::move(catalog));

    const auto data_path = resolve(
        context.project->ic_catalog_path().parent_path(),
        removed_path);

    // The catalog is authoritative after publication. Failure to remove
    // the unreferenced binary is cleanup only and does not roll back DELETE_IC.
    std::error_code ignored;
    (void)std::filesystem::remove(
        data_path,
        ignored);

    return server_status::success;
}

server_status server::list_ic(
    ic_catalog& output,
    operation_id operation,
    diagnostic_collection& diagnostics) {

    output = {};

    if (!context.project) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_not_loaded,
                operation)
                .detail("LIST_IC requires an active Project")
                .build());
        return server_status::project_not_loaded;
    }

    if (!succeeded(require_ic_catalog(
            *context.project,
            operation,
            diagnostics))) {
        return server_status::runtime_ic_invalid;
    }

    try {
        output =
            context.project->ic_catalog_data();
    }
    catch (...) {
        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_ic_failed,
                operation)
                .detail(
                    "Cannot allocate LIST_IC response")
                .build());

        return server_status::runtime_ic_failed;
    }

    return server_status::success;
}

server_status server::run_project(
    operation_id operation,
    diagnostic_collection& diagnostics) {

    if (!context.project) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_not_loaded,
                operation)
                .detail("RUN requires an active Project")
                .build());

        return server_status::project_not_loaded;
    }

    if (!project_state_can_run(
            project_state_value)) {

        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_state_invalid,
                operation)
                .detail("RUN requires LOADED or FREEZE")
                .build());

        return server_status::runtime_state_invalid;
    }

    std::unique_lock<std::mutex> scheduler_lock(scheduler_mutex, std::defer_lock);
    if (scheduler_thread_enabled) scheduler_lock.lock();
    if (task_runtime_failed) {
        diagnostics.emit(diagnostic(diagnostics::runtime_state_invalid, operation)
            .detail("Task failure requires UNLOAD").build());
        return server_status::project_runtime_failed;
    }
    if (project_state_value == project_state::loaded ||
        project_state_value == project_state::stopped) {
        runtime_tick = 0;
        model_cycle = 0;
        const auto& shm = context.project->shm();
        const auto& config = context.project->tasks();
        if (!config.tasks.empty()) {
            const auto name = shm.name();
            if (!shm.valid() || name.empty() || name.size() > 128) {
                diagnostics.emit(diagnostic(diagnostics::runtime_state_invalid, operation)
                    .detail("Task SHM name/address unavailable").build());
                return server_status::project_runtime_failed;
            }
            task_runtime::shm_attachment attachment{};
            attachment.size = static_cast<std::uint64_t>(shm.size());
            attachment.address = static_cast<std::uint64_t>(shm.address());
            std::memcpy(attachment.name, name.data(), name.size());
            std::string error;
            if (!task_manager.start(config, context.project->path().parent_path(),
                    5000, attachment, error)) {
                diagnostics.emit(diagnostic(diagnostics::runtime_state_invalid, operation)
                    .detail("Task startup failed: " + error).build());
                return server_status::project_runtime_failed;
            }
        }
    }
    const auto& config_timing = context.project->tasks().timing;
    scheduler_realtime_active = config_timing.mode == task_timing_mode::real_time;
    scheduler_project_period_us = config_timing.period_ms
        ? config_timing.period_ms * 1000 : scheduler_default_period_us;
    task_system_table.set_period_us(scheduler_project_period_us);
    task_system_table.on_run(project_state_value != project_state::freeze);
    // Task workloads always use the dedicated scheduler so KILL_TASK can
    // preempt a hung TASKMAIN. Empty Task configs retain previous behavior.
    const char* requested_thread = std::getenv("TASK_SCHEDULER_THREAD");
    scheduler_dedicated_active = !context.project->tasks().tasks.empty() ||
        scheduler_realtime_active ||
        (requested_thread && std::strcmp(requested_thread, "1") == 0);
    next_task_tick = std::chrono::steady_clock::now();
    set_project_state(project_state::run);
    if (scheduler_thread_enabled) scheduler_cv.notify_all();

    return server_status::success;
}

server_status server::freeze_project(
    operation_id operation,
    diagnostic_collection& diagnostics) {
    std::unique_lock<std::mutex> scheduler_lock(scheduler_mutex, std::defer_lock);
    if (scheduler_thread_enabled) scheduler_lock.lock();

    if (!context.project) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_not_loaded,
                operation)
                .detail("FREEZE requires an active Project")
                .build());

        return server_status::project_not_loaded;
    }

    if (!project_state_can_freeze(
            project_state_value)) {

        diagnostics.emit(
            diagnostic(
                diagnostics::runtime_state_invalid,
                operation)
                .detail("FREEZE requires RUN")
                .build());

        return server_status::runtime_state_invalid;
    }

    // The scheduler mutex prevents new ticks while outstanding NO_BARRIER
    // requests finish. Do not acknowledge FREEZE while SHM writers remain.
    if (std::getenv("TASK_PERF52_NO_BARRIER") &&
        std::strcmp(std::getenv("TASK_PERF52_NO_BARRIER"), "1") == 0) {
        std::string task_error;
        if (!task_manager.quiesce(task_error)) {
            task_runtime_failed = true;
            task_manager.terminate();
            diagnostics.emit(diagnostic(diagnostics::runtime_state_invalid, operation)
                .detail("Task FREEZE drain failed: " + task_error).build());
            set_project_state(project_state::freeze);
            return server_status::project_runtime_failed;
        }
    }
    set_project_state(project_state::freeze);
    task_system_table.on_pause();

    return server_status::success;
}

// KILL_TASK is per-process, not a Project STOP command.
server_status server::kill_task(
    const std::string& id, operation_id operation,
    diagnostic_collection& diagnostics) {
    if (!context.project) {
        diagnostics.emit(diagnostic(diagnostics::project_not_loaded, operation)
            .detail("KILL_TASK requires a loaded Project").build());
        return server_status::project_not_loaded;
    }
    // Do not acquire scheduler_mutex or read scheduler-owned state yet:
    // this control must be able to preempt a hung TASKMAIN.
    std::size_t index = 0;
    std::string error;
    // Preempt BEFORE acquiring scheduler_mutex. Only this Task exits;
    // the scheduler treats it as a satisfied dependency in this cycle.
    if (!task_manager.request_kill(id, index, error)) {
        diagnostics.emit(diagnostic(diagnostics::runtime_state_invalid, operation)
            .detail(error).build());
        return server_status::runtime_state_invalid;
    }
    std::unique_lock<std::mutex> lock(scheduler_mutex);
    task_manager.finish_kill(index);
    if (task_manager.active_count() != 0) {
        next_task_tick = std::chrono::steady_clock::now();
        scheduler_resync_requested = true;
    }
    if (task_manager.active_count() == 0) {
        scheduler_dedicated_active = false;
        task_system_table.on_stop(static_cast<std::uint32_t>(project_state::stopped));
        set_project_state(project_state::stopped);
    }
    return server_status::success;
}

server_status server::start_task(
    const std::string& id, operation_id operation,
    diagnostic_collection& diagnostics) {
    if (!context.project) {
        diagnostics.emit(diagnostic(diagnostics::project_not_loaded, operation)
            .detail("START_TASK requires a loaded Project").build());
        return server_status::project_not_loaded;
    }
    // TASKINIT is serialized at the cycle boundary; other Task processes
    // remain alive and resume at the next cycle after initialization.
    std::unique_lock<std::mutex> lock(scheduler_mutex);
    if (!task_manager.active() ||
        (project_state_value != project_state::run &&
         project_state_value != project_state::freeze &&
         project_state_value != project_state::stopped)) {
        diagnostics.emit(diagnostic(diagnostics::runtime_state_invalid, operation)
            .detail("START_TASK requires a running/paused Task Manager").build());
        return server_status::runtime_state_invalid;
    }
    std::string error;
    std::size_t index = 0;
    if (!task_manager.restart_one(id, index, error)) {
        diagnostics.emit(diagnostic(diagnostics::runtime_state_invalid, operation)
            .detail(error).build());
        return server_status::runtime_state_invalid;
    }
    next_task_tick = std::chrono::steady_clock::now();
    scheduler_resync_requested = true;
    if (project_state_value == project_state::stopped) {
        task_system_table.resume_after_all_killed();
        scheduler_realtime_active =
            context.project->tasks().timing.mode == task_timing_mode::real_time;
        scheduler_dedicated_active = true;
        next_task_tick = std::chrono::steady_clock::now();
        set_project_state(project_state::run);
        scheduler_cv.notify_all();
    }
    return server_status::success;
}

server_status server::unload(
    operation_id operation,
    diagnostic_collection& diagnostics) {
    std::unique_lock<std::mutex> scheduler_lock(scheduler_mutex, std::defer_lock);
    if (scheduler_thread_enabled) scheduler_lock.lock();

    if (!context.project) {
        diagnostics.emit(
            diagnostic(
                diagnostics::project_not_loaded,
                operation)
                .detail("UNLOAD requires an active Project")
                .build());

        return server_status::project_not_loaded;
    }

    // FAULT-17-FIX2: An explicit UNLOAD may recover a RUN Project only when
    // Task runtime has failed. Never release SHM while the scheduler can tick.
    const bool emergency_unload =
        project_state_value == project_state::run && task_runtime_failed;
    if (!project_state_can_unload(project_state_value) && !emergency_unload) {
        diagnostics.emit(
            diagnostic(diagnostics::runtime_state_invalid, operation)
                .detail("UNLOAD requires LOADED or FREEZE (or failed Task runtime)")
                .build());
        return server_status::runtime_state_invalid;
    }

    // scheduler_mutex is held above when the scheduler thread is enabled.
    // Disarm dispatch before stopping Task processes or unmapping Project SHM.
    if (emergency_unload) set_project_state(project_state::freeze);
    std::string task_error;
    const bool stopped = !task_manager.active() || task_manager.stop(task_error);
    if (!stopped) {
        task_runtime_failed = true;
        diagnostics.emit(diagnostic(diagnostics::runtime_state_invalid, operation)
            .detail("Task STOP failed; Project SHM retained: " + task_error).build());
        return server_status::project_runtime_failed;
    }
    task_runtime_failed = false;
    runtime_tick = 0;
    model_cycle = 0;
    scheduler_dedicated_active = false;
    scheduler_realtime_active = false;
    scheduler_project_period_us = scheduler_default_period_us;
    task_manager.set_system_table(nullptr);
    task_system_table.close();
    context.project.reset();
    set_project_state(project_state::unloaded);
    scheduler_cv.notify_all();
    if (const char* path = std::getenv("TASK_UNLOAD_OWNERSHIP_11_LOG")) {
        const auto fixed = fixed_shared_memory_snapshot();
        const auto ipc = task_runtime::task_ipc_snapshot();
        FILE* file = nullptr;
        if (fopen_s(&file, path, "ab") == 0 && file) {
            std::fprintf(file, "%llu,%llu,%llu,%llu\n",
                static_cast<unsigned long long>(fixed.handles),
                static_cast<unsigned long long>(fixed.views),
                static_cast<unsigned long long>(ipc.handles),
                static_cast<unsigned long long>(ipc.views));
            std::fclose(file);
        }
    }
    if (std::getenv("TASK_UNLOAD_OWNERSHIP_10")) {
        const auto ownership = fixed_shared_memory_snapshot();
        DWORD handles = 0;
        const BOOL handle_ok = GetProcessHandleCount(GetCurrentProcess(), &handles);
        std::fprintf(stderr,
            "TASK_UNLOAD_OWNERSHIP_10 fixed_handles=%llu fixed_views=%llu process_handles=%lu process_handles_valid=%d\n",
            static_cast<unsigned long long>(ownership.handles),
            static_cast<unsigned long long>(ownership.views),
            static_cast<unsigned long>(handles), handle_ok ? 1 : 0);
        std::fflush(stderr);
    }

#if defined(CW_UNLOAD_HEAP_OPTIMIZATION)
    optimize_heap_after_unload(operation, diagnostics);
#endif

    return stopped ? server_status::success : server_status::project_runtime_failed;
}

void server::shutdown() noexcept {
    // run() has already joined the scheduler thread.
    running = false;
    context.requests.stop_accepting_and_discard();
    context.communications.stop();
    std::string task_error;
    if (task_manager.active()) (void)task_manager.stop(task_error);
    task_manager.terminate();
    task_manager.set_system_table(nullptr);
    task_system_table.close();
    context.project.reset();
    set_project_state(project_state::unloaded);
    context.authentication.stop();
    context.lease.clear();
    context.identity.stop();
    context.license.clear();
}



}
