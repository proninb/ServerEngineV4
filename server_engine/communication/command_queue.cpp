/*
 * command_queue implementation.
 *
 * The mutex protects only control-plane command traffic. Runtime execution
 * must never depend on this queue.
 */
#include "command_queue.hpp"

#include <utility>

namespace cw::server {

// Publishes one command atomically with respect to the queue and wakes one consumer.
void command_queue::push(server_command_request request) {
    {
        // Hold the lock only for the FIFO mutation.
        std::lock_guard lock(mutex);
        queue.push(std::move(request));
    }

    // Notification is intentionally issued after releasing the mutex.
    condition.notify_one();
}

// Sleeps until work exists, then transfers ownership of the oldest queued command.
server_command_request command_queue::wait_pop() {
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
