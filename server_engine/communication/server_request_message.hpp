/*
 * Queued Server request plus direct reply destination.
 *
 * The origin is non-owning and performs no endpoint lookup.
 */
#pragma once

#include "connection/connection_lifetime.hpp"
#include "request_id.hpp"
#include "request_identity.hpp"
#include "server_request.hpp"

#include <utility>

namespace cw::server {

struct server_response;

// Direct response target for one queued request.
class server_request_origin final {
public:
    using present_function =
        void (*)(
            void* context,
            request_id request,
            const server_response& result);

    server_request_origin() noexcept = default;

    server_request_origin(
        void* context,
        present_function present,
        request_id request = {},
        connection_lifetime_token lifetime = {}) noexcept
        : context(context),
          present_callback(present),
          request(request),
          lifetime(std::move(lifetime)) {
    }

    server_request_origin(server_request_origin&&) noexcept = default;
    server_request_origin& operator=(server_request_origin&&) noexcept = default;

    server_request_origin(const server_request_origin&) = delete;
    server_request_origin& operator=(const server_request_origin&) = delete;

    // Presents a completed response to the originating endpoint.
    void present(
        const server_response& result) const {

        if (present_callback != nullptr) {
            present_callback(
                context,
                request,
                result);
        }
    }

private:
    // Non-owning endpoint pointer.
    void* context = nullptr;

    // Endpoint-specific direct presentation callback.
    present_function present_callback = nullptr;

    request_id request;

    // Keeps the response target alive until the queued request is destroyed.
    connection_lifetime_token lifetime;
};

// Transport-neutral request envelope stored in request_queue.
struct server_request_message {
    server_request request;

    // Origin/application metadata evaluated by Server Policy.
    request_identity identity;

    // Direct response destination; not used for access decisions.
    server_request_origin origin;
};

}
