/*
 * Queued Server request plus direct reply destination.
 *
 * The origin is non-owning and performs no endpoint lookup.
 */
#pragma once

#include "server_request.hpp"

namespace cw::server {

struct server_response;

// Direct response target for one queued request.
class server_request_origin final {
public:
    using present_function =
        void (*)(
            void* context,
            const server_response& result);

    constexpr server_request_origin() noexcept = default;

    constexpr server_request_origin(
        void* context,
        present_function present) noexcept
        : context(context),
          present_callback(present) {
    }

    // Presents a completed response to the originating endpoint.
    void present(
        const server_response& result) const {

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

// Transport-neutral request envelope stored in request_queue.
struct server_request_message {
    server_request request;
    server_request_origin origin;
};

}
