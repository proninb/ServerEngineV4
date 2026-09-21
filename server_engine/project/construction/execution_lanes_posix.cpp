#include "execution_lanes.hpp"

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

#include <memory>
#include <new>

namespace cw::server {
namespace {

struct lane_pool_state;

struct lane_worker final {
    lane_pool_state* owner = nullptr;
    std::size_t lane = 0;

    sem_t start_sem{};
    sem_t done_sem{};

    pthread_t thread{};

    bool start_initialized = false;
    bool done_initialized = false;
    bool thread_started = false;
};

struct lane_pool_state final {
    std::unique_ptr<lane_worker[]> workers;
    std::size_t lane_count = 0;
    std::size_t worker_count = 0;

    execution_lane_function function = nullptr;
    void* context = nullptr;
    bool stopping = false;
};

[[nodiscard]] bool wait_sem(
    sem_t& value) noexcept {

    for (;;) {
        if (sem_wait(
                &value) == 0) {

            return true;
        }

        if (errno != EINTR) {
            return false;
        }
    }
}

void* lane_entry(
    void* value) noexcept {

    auto& worker =
        *static_cast<lane_worker*>(
            value);

    for (;;) {
        if (!wait_sem(
                worker.start_sem)) {

            return nullptr;
        }

        auto& state =
            *worker.owner;

        if (state.stopping) {
            return nullptr;
        }

        const auto function =
            state.function;

        if (function == nullptr) {
            return nullptr;
        }

        function(
            state.context,
            worker.lane);

        if (sem_post(
                &worker.done_sem) != 0) {

            return nullptr;
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

        if (worker.thread_started &&
            worker.start_initialized) {

            (void)sem_post(
                &worker.start_sem);
        }
    }

    for (std::size_t index = 0;
         index < state->worker_count;
         ++index) {

        auto& worker =
            state->workers[index];

        if (worker.thread_started) {
            (void)pthread_join(
                worker.thread,
                nullptr);

            worker.thread_started =
                false;
        }

        if (worker.start_initialized) {
            (void)sem_destroy(
                &worker.start_sem);

            worker.start_initialized =
                false;
        }

        if (worker.done_initialized) {
            (void)sem_destroy(
                &worker.done_sem);

            worker.done_initialized =
                false;
        }
    }

    delete state;
}

}

std::size_t execution_lane_capacity() noexcept {

    const auto count =
        sysconf(
            _SC_NPROCESSORS_ONLN);

    return count <= 0
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

        if (sem_init(
                &worker.start_sem,
                0,
                0) != 0) {

            destroy_state(
                candidate);

            return server_status::io_error;
        }

        worker.start_initialized =
            true;

        if (sem_init(
                &worker.done_sem,
                0,
                0) != 0) {

            destroy_state(
                candidate);

            return server_status::io_error;
        }

        worker.done_initialized =
            true;

        if (pthread_create(
                &worker.thread,
                nullptr,
                lane_entry,
                &worker) != 0) {

            destroy_state(
                candidate);

            return server_status::io_error;
        }

        worker.thread_started =
            true;
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

        if (sem_post(
                &worker.start_sem) != 0) {

            for (std::size_t completed = 0;
                 completed < started;
                 ++completed) {

                (void)wait_sem(
                    current->workers[completed]
                        .done_sem);
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

        if (!wait_sem(
                current->workers[index]
                    .done_sem)) {

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
