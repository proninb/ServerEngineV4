/*
 * Control-plane multi-producer/single-consumer request queue.
 *
 * Communication endpoint threads are producers. The Server control thread is
 * the single consumer. Transports publish parsed server_request values here;
 * Runtime hot paths never depend on this queue.
 */
#pragma once

#include "server_request_message.hpp"

#include <condition_variable>
#include <mutex>
#include <queue>

namespace cw::server {

// Synchronization boundary between asynchronous communication and Server request execution.
class request_queue final {
public:
    // Publishes one complete request message and wakes the waiting Server control thread.
    void push(server_request_message request);

    // Blocks without spinning until a request is available, then removes and returns it.
    [[nodiscard]] server_request_message wait_pop();

private:
    // Protects queue mutation and inspection performed by producers/consumer.
    std::mutex mutex;

    // Wakes the Server control thread when a producer publishes work.
    std::condition_variable condition;

    // FIFO preserves external request arrival order at this synchronization boundary.
    std::queue<server_request_message> queue;
};

}
