/*
 * Control-plane multi-producer/single-consumer request queue.
 *
 * Communication endpoint threads are producers. The Server control thread is
 * the single consumer. Transports publish parsed server_request values here;
 * Runtime hot paths never depend on this queue.
 */
#pragma once

#include "control/communication_control.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>

namespace cw::server {

// Synchronization boundary between asynchronous communication and Server request execution.
class request_queue final {
public:
    // Publishes one complete request message and wakes the waiting Server control thread.
    // Returns false after shutdown has stopped request acceptance.
    [[nodiscard]] bool push(communication_control_message request);

    // Rejects future input and destroys queued-but-not-started requests.
    void stop_accepting_and_discard() noexcept;

    // Blocks until work arrives or request acceptance is stopped.
    [[nodiscard]] bool wait_pop(
        communication_control_message& output);

    // Waits until work arrives or the absolute control-plane deadline is reached.
    [[nodiscard]] bool wait_pop_until(
        std::chrono::system_clock::time_point deadline,
        communication_control_message& output);

private:
    // Protects queue mutation and inspection performed by producers/consumer.
    std::mutex mutex;

    // Wakes the Server control thread when a producer publishes work.
    std::condition_variable condition;

    // FIFO preserves external request arrival order at this synchronization boundary.
    std::queue<communication_control_message> queue;

    bool accepting = true;
};

}
