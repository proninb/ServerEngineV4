/*
 * Optional console communication endpoint.
 *
 * A server_console exists only when server.json declares transport=console.
 * It owns one input thread and converts complete console lines into commands.
 */
#pragma once

#include "../command_queue.hpp"
#include "../server_command_result.hpp"
#include "console_input.hpp"

#include <atomic>
#include <thread>

namespace cw::server {

// Console transport feeding the shared Server command queue.
class server_console final {
public:
    // Ensures the input thread and platform resources are stopped on destruction.
    ~server_console();

    // Opens console input and starts the endpoint thread.
    // Returns false when required platform resources cannot be created.
    [[nodiscard]] bool start(command_queue& commands);

    // Interrupts pending input, joins the thread, and releases console resources.
    void stop() noexcept;

private:
    // Endpoint thread body: read line -> parse -> publish server_command.
    void run();

    // Publishes one parsed command with this console as its reply target.
    void publish(
        server_command command);

    // Direct reply callback used by server_command_origin.
    static void present_result(
        void* context,
        const server_command_result& result);

    // Presents one completed command result.
    void present(
        const server_command_result& result);

    // Non-owning command destination valid only while the endpoint is running.
    command_queue* commands = nullptr;

    // Interruptible platform-neutral terminal input backend.
    console_input input;

    // Dedicated console input thread.
    std::jthread thread;

    // Guards start/stop state and allows run() to observe shutdown.
    std::atomic_bool running = false;
};

}
