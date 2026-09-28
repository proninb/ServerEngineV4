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
void request_queue::push(server_request_message request) {
    {
        // Hold the lock only for the FIFO mutation.
        std::lock_guard lock(mutex);
        queue.push(std::move(request));
    }

    // Notification is intentionally issued after releasing the mutex.
    condition.notify_one();
}

// Sleeps until work exists, then transfers ownership of the oldest queued command.
server_request_message request_queue::wait_pop() {
    std::unique_lock lock(mutex);

    // Predicate handles spurious condition_variable wakeups.
    condition.wait(lock, [this] {
        return !queue.empty();
    });

    auto request = std::move(queue.front());
    queue.pop();

    return request;
}

bool request_queue::wait_pop_until(
    std::chrono::system_clock::time_point deadline,
    server_request_message& output) {

    std::unique_lock lock(mutex);

    if (!condition.wait_until(
            lock,
            deadline,
            [this] {
                return !queue.empty();
            })) {

        return false;
    }

    output = std::move(queue.front());
    queue.pop();
    return true;
}

}
