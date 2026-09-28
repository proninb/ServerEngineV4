/*
 * request_queue implementation.
 *
 * The mutex protects only control-plane request traffic. Runtime execution
 * must never depend on this queue.
 */
#include "request_queue.hpp"

#include <utility>

namespace cw::server {

// Publishes one request atomically with respect to the queue and wakes one consumer.
bool request_queue::push(communication_control_message request) {
    {
        std::lock_guard lock(mutex);

        if (!accepting) {
            return false;
        }

        queue.push(std::move(request));
    }

    condition.notify_one();
    return true;
}

void request_queue::stop_accepting_and_discard() noexcept {
    std::queue<communication_control_message> discarded;

    {
        std::lock_guard lock(mutex);
        accepting = false;
        discarded.swap(queue);
    }

    condition.notify_all();
}

// Sleeps until work exists or shutdown stops request acceptance.
bool request_queue::wait_pop(
    communication_control_message& output) {

    std::unique_lock lock(mutex);

    condition.wait(lock, [this] {
        return !queue.empty() || !accepting;
    });

    if (queue.empty()) {
        return false;
    }

    output = std::move(queue.front());
    queue.pop();
    return true;
}

bool request_queue::wait_pop_until(
    std::chrono::system_clock::time_point deadline,
    communication_control_message& output) {

    std::unique_lock lock(mutex);

    if (!condition.wait_until(
            lock,
            deadline,
            [this] {
                return !queue.empty() || !accepting;
            })) {

        return false;
    }

    if (queue.empty()) {
        return false;
    }

    output = std::move(queue.front());
    queue.pop();
    return true;
}

}
