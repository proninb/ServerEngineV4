/*
 * Control-plane multi-producer/single-consumer command queue.
 *
 * Communication endpoint threads are producers. The Server control thread is
 * the single consumer. This queue is intentionally not used by Runtime hot paths.
 */
#pragma once

#include "server_command_request.hpp"

#include <condition_variable>
#include <mutex>
#include <queue>

namespace cw::server {

// Synchronization boundary between asynchronous communication and Server lifecycle execution.
class command_queue final {
public:
    // Publishes one complete command and wakes the waiting Server control thread.
    void push(server_command_request request);

    // Blocks without spinning until a command is available, then removes and returns it.
    [[nodiscard]] server_command_request wait_pop();

private:
    // Protects queue mutation and inspection performed by producers/consumer.
    std::mutex mutex;

    // Wakes the Server control thread when a producer publishes work.
    std::condition_variable condition;

    // FIFO preserves external command arrival order at this synchronization boundary.
    std::queue<server_command_request> queue;
};

}
