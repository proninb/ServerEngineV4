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
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <queue>

namespace cw::server {

inline constexpr std::size_t
request_queue_default_message_limit = 1024;

inline constexpr std::size_t
request_queue_default_byte_limit =
    16u * 1024u * 1024u;

struct request_queue_limits final {
    std::size_t messages =
        request_queue_default_message_limit;
    std::size_t bytes =
        request_queue_default_byte_limit;
};

enum class request_queue_push_result : std::uint8_t {
    accepted = 0,
    stopped,
    overloaded,
    failed,
};

// Synchronization boundary between asynchronous communication and Server request execution.
class request_queue final {
public:
    explicit request_queue(
        request_queue_limits limits = {}) noexcept
        : limits(limits) {
    }

    [[nodiscard]] request_queue_push_result try_push(
        communication_control_message request);

    // Publishes one complete request message and wakes the waiting Server control thread.
    // Returns false after shutdown or when the bounded ingress budget is exhausted.
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
    struct queued_message final {
        communication_control_message message;
        std::size_t accounted_bytes = 0;
        bool accounted = false;
    };

    [[nodiscard]] static std::size_t estimate_message_bytes(
        const communication_control_message& message) noexcept;

    // Protects queue mutation and inspection performed by producers/consumer.
    std::mutex mutex;

    // Wakes the Server control thread when a producer publishes work.
    std::condition_variable condition;

    // FIFO preserves external request arrival order at this synchronization boundary.
    std::queue<queued_message> queue;

    request_queue_limits limits;
    std::size_t accounted_messages = 0;
    std::size_t accounted_bytes = 0;

    bool accepting = true;
};

}
