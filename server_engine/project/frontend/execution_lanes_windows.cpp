#include "execution_lanes.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <process.h>

#include <cstdint>
#include <memory>
#include <new>

namespace cw::server {
namespace {

struct lane_pool_state;

struct lane_worker final {
    lane_pool_state* owner = nullptr;
    std::size_t lane = 0;
    HANDLE start_event = nullptr;
    HANDLE done_event = nullptr;
    HANDLE thread = nullptr;
};

struct lane_pool_state final {
    std::unique_ptr<lane_worker[]> workers;
    std::size_t lane_count = 0;
    std::size_t worker_count = 0;

    execution_lane_function function = nullptr;
    void* context = nullptr;
    bool stopping = false;
};

unsigned __stdcall lane_entry(
    void* value) noexcept {

    auto& worker =
        *static_cast<lane_worker*>(
            value);

    for (;;) {
        if (WaitForSingleObject(
                worker.start_event,
                INFINITE) !=
            WAIT_OBJECT_0) {

            return 1;
        }

        auto& state =
            *worker.owner;

        if (state.stopping) {
            return 0;
        }

        const auto function =
            state.function;

        if (function == nullptr) {
            return 1;
        }

        function(
            state.context,
            worker.lane);

        if (!SetEvent(
                worker.done_event)) {

            return 1;
        }
    }
}

void destroy_state(
    lane_pool_state* state) noexcept {

    if (state == nullptr) {
        return;
    }

    state->stopping = true;

    for (std::size_t index = 0;
         index < state->worker_count;
         ++index) {

        auto& worker =
            state->workers[index];

        if (worker.thread != nullptr &&
            worker.start_event != nullptr) {

            (void)SetEvent(
                worker.start_event);
        }
    }

    for (std::size_t index = 0;
         index < state->worker_count;
         ++index) {

        auto& worker =
            state->workers[index];

        if (worker.thread != nullptr) {
            (void)WaitForSingleObject(
                worker.thread,
                INFINITE);

            CloseHandle(
                worker.thread);

            worker.thread = nullptr;
        }

        if (worker.start_event != nullptr) {
            CloseHandle(
                worker.start_event);

            worker.start_event = nullptr;
        }

        if (worker.done_event != nullptr) {
            CloseHandle(
                worker.done_event);

            worker.done_event = nullptr;
        }
    }

    delete state;
}

}

std::size_t execution_lane_capacity() noexcept {

    const auto count =
        GetActiveProcessorCount(
            ALL_PROCESSOR_GROUPS);

    return count == 0
        ? std::size_t{1}
        : static_cast<std::size_t>(
            count);
}

execution_lanes::~execution_lanes() noexcept {

    destroy_state(
        static_cast<lane_pool_state*>(
            state));

    state = nullptr;
}

server_status execution_lanes::start(
    std::size_t lane_count) noexcept {

    if (state != nullptr ||
        lane_count == 0) {

        return server_status::project_configuration_invalid;
    }

    auto* candidate =
        new (std::nothrow)
            lane_pool_state;

    if (candidate == nullptr) {
        return server_status::io_error;
    }

    candidate->lane_count =
        lane_count;

    candidate->worker_count =
        lane_count - 1;

    if (candidate->worker_count != 0) {
        candidate->workers.reset(
            new (std::nothrow)
                lane_worker[
                    candidate->worker_count]);

        if (!candidate->workers) {
            delete candidate;
            return server_status::io_error;
        }
    }

    for (std::size_t index = 0;
         index < candidate->worker_count;
         ++index) {

        auto& worker =
            candidate->workers[index];

        worker.owner =
            candidate;

        worker.lane =
            index + 1;

        worker.start_event =
            CreateEventW(
                nullptr,
                FALSE,
                FALSE,
                nullptr);

        worker.done_event =
            CreateEventW(
                nullptr,
                FALSE,
                FALSE,
                nullptr);

        if (worker.start_event == nullptr ||
            worker.done_event == nullptr) {

            destroy_state(
                candidate);

            return server_status::io_error;
        }

        const auto raw =
            _beginthreadex(
                nullptr,
                0,
                lane_entry,
                &worker,
                0,
                nullptr);

        if (raw == 0) {
            destroy_state(
                candidate);

            return server_status::io_error;
        }

        worker.thread =
            reinterpret_cast<HANDLE>(
                static_cast<std::uintptr_t>(
                    raw));
    }

    state =
        candidate;

    return server_status::success;
}

server_status execution_lanes::run(
    std::size_t active_lanes,
    execution_lane_function function,
    void* context) noexcept {

    auto* current =
        static_cast<lane_pool_state*>(
            state);

    if (current == nullptr ||
        active_lanes == 0 ||
        active_lanes > current->lane_count ||
        function == nullptr) {

        return server_status::project_configuration_invalid;
    }

    current->function =
        function;

    current->context =
        context;

    std::size_t started = 0;

    for (std::size_t lane = 1;
         lane < active_lanes;
         ++lane) {

        auto& worker =
            current->workers[
                lane - 1];

        if (!SetEvent(
                worker.start_event)) {

            for (std::size_t completed = 0;
                 completed < started;
                 ++completed) {

                (void)WaitForSingleObject(
                    current->workers[completed]
                        .done_event,
                    INFINITE);
            }

            current->function =
                nullptr;

            current->context =
                nullptr;

            return server_status::io_error;
        }

        ++started;
    }

    function(
        context,
        0);

    server_status status =
        server_status::success;

    for (std::size_t index = 0;
         index < started;
         ++index) {

        if (WaitForSingleObject(
                current->workers[index]
                    .done_event,
                INFINITE) !=
            WAIT_OBJECT_0) {

            status =
                server_status::io_error;
        }
    }

    current->function =
        nullptr;

    current->context =
        nullptr;

    return status;
}

std::size_t execution_lanes::size() const noexcept {

    const auto* current =
        static_cast<const lane_pool_state*>(
            state);

    return current == nullptr
        ? 0
        : current->lane_count;
}

}
