/*
 * Queued Server command plus direct reply destination.
 *
 * The origin is non-owning and performs no endpoint lookup.
 */
#pragma once

#include "server_command.hpp"

namespace cw::server {

struct server_command_result;

// Direct response target for one queued command.
class server_command_origin final {
public:
    using present_function =
        void (*)(
            void* context,
            const server_command_result& result);

    constexpr server_command_origin() noexcept = default;

    constexpr server_command_origin(
        void* context,
        present_function present) noexcept
        : context(context),
          present_callback(present) {
    }

    // Presents a completed command result to its originating endpoint.
    void present(
        const server_command_result& result) const {

        if (present_callback != nullptr) {
            present_callback(
                context,
                result);
        }
    }

private:
    // Non-owning endpoint pointer.
    void* context = nullptr;

    // Endpoint-specific direct presentation callback.
    present_function present_callback = nullptr;
};

// Transport-neutral command request stored in command_queue.
struct server_command_request {
    server_command command;
    server_command_origin origin;
};

}
