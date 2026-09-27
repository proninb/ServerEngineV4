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

}
