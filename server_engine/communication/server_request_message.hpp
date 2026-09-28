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

#include <chrono>
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

    using arm_written_function =
        void (*)(
            void* context,
            request_id request) noexcept;

    using wait_written_function =
        bool (*)(
            void* context,
            request_id request,
            std::chrono::milliseconds timeout);

    server_request_origin() noexcept = default;

    server_request_origin(
        void* context,
        present_function present,
        request_id request = {},
        connection_lifetime_token lifetime = {},
        arm_written_function arm_written = nullptr,
        wait_written_function wait_written = nullptr) noexcept
        : context(context),
          present_callback(present),
          arm_written_callback(arm_written),
          wait_written_callback(wait_written),
          request(request),
          lifetime(std::move(lifetime)) {
    }

    server_request_origin(server_request_origin&&) noexcept = default;
    server_request_origin& operator=(server_request_origin&&) noexcept = default;

    server_request_origin(const server_request_origin&) = delete;
    server_request_origin& operator=(const server_request_origin&) = delete;

    void arm_written() const noexcept {
        if (arm_written_callback != nullptr) {
            arm_written_callback(
                context,
                request);
        }
    }

    [[nodiscard]] bool wait_written(
        std::chrono::milliseconds timeout) const {

        return wait_written_callback == nullptr ||
            wait_written_callback(
                context,
                request,
                timeout);
    }

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
    arm_written_function arm_written_callback = nullptr;
    wait_written_function wait_written_callback = nullptr;

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
