/*
 * Optional console communication endpoint.
 *
 * A server_console exists only when server.json declares transport=console.
 * It owns one input thread and converts complete console lines into transport-neutral requests.
 */
#pragma once

#include "../request_queue.hpp"
#include "../server_response.hpp"
#include "console_input.hpp"

#include <atomic>
#include <thread>

namespace cw::server {

// Console transport feeding the shared Server request queue.
class server_console final {
public:
    // Ensures the input thread and platform resources are stopped on destruction.
    ~server_console();

    // Opens console input and starts the endpoint thread.
    // Returns false when required platform resources cannot be created.
    [[nodiscard]] bool start(request_queue& requests);

    // Interrupts pending input, joins the thread, and releases console resources.
    void stop() noexcept;

private:
    // Endpoint thread body: read line -> parse -> publish server_request.
    void run();

    // Publishes one parsed request with this console as its reply target.
    void publish(
        server_request request);

    // Direct reply callback used by server_request_origin.
    static void present_result(
        void* context,
        const server_response& result);

    // Presents one completed request response.
    void present(
        const server_response& result);

    // Non-owning request destination valid only while the endpoint is running.
    request_queue* requests = nullptr;

    // Interruptible platform-neutral terminal input backend.
    console_input input;

    // Dedicated console input thread.
    std::jthread thread;

    // Guards start/stop state and allows run() to observe shutdown.
    std::atomic_bool running = false;
};

}
