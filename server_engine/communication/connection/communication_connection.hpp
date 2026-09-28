/*
 * Transport-independent Communication connection core.
 *
 * One transport connection owns one logical session. Reconnect creates a new
 * session; V1 has no resume.
 *
 * LOGIN and subscription-registry mutation are Server-control-thread only.
 */
#pragma once

#include "connection_lifetime.hpp"
#include "outbound_channel.hpp"
#include "subscription_registration.hpp"
#include "../session/client_session.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace cw::server {

class communication_connection final {
public:
    static communication_connection_owner create(
        client_session_id id,
        std::size_t outbound_byte_limit);

    communication_connection(const communication_connection&) = delete;
    communication_connection& operator=(const communication_connection&) = delete;

    [[nodiscard]] client_session_id id() const noexcept;
    [[nodiscard]] bool logged_in() const noexcept;

    // Server-control-thread only. Name is immutable after successful LOGIN.
    [[nodiscard]] std::string_view login_name() const noexcept;

    // Server-control-thread only.
    [[nodiscard]] client_session_login_result commit_login(
        std::string_view name) noexcept;

    // Communication boundary: reject invalid/duplicate outstanding request IDs.
    [[nodiscard]] bool reserve_request(request_id request);

    // Single-writer only: called when response becomes next outbound frame.
    void release_request(request_id request) noexcept;

    [[nodiscard]] bool request_outstanding(request_id request) const;

    // Cheap/non-blocking Server completion path: enqueue only.
    [[nodiscard]] bool enqueue_response(
        request_id request,
        const server_response& result);

    // Server-control-thread routing paths.
    [[nodiscard]] bool enqueue_server_state(
        project_state old_state,
        project_state new_state);

    [[nodiscard]] bool enqueue_client_action(client_action action);

    [[nodiscard]] bool enqueue_subscription_data(subscription_data data);

    // Single writer. connection_sequence is assigned by outbound_channel.
    [[nodiscard]] bool wait_next(outbound_write& output);

    void mark_written(std::uint64_t connection_sequence) noexcept;

    [[nodiscard]] bool wait_until_written(
        std::uint64_t connection_sequence,
        std::chrono::milliseconds timeout);

    // Any thread may initiate close. Does not mutate subscriptions.
    [[nodiscard]] bool begin_close() noexcept;
    [[nodiscard]] bool closing() const noexcept;

    // Server-control-thread only.
    [[nodiscard]] bool add_subscription(
        subscription_id id,
        subscription_registration registration);

    [[nodiscard]] bool remove_subscription(
        subscription_id id) noexcept;

    // Server-control-thread only; destruction of tokens deregisters publishers.
    void teardown_subscriptions() noexcept;

    [[nodiscard]] std::size_t subscription_count() const noexcept;

    [[nodiscard]] connection_lifetime_token hold() noexcept;

    static void present_response(
        void* context,
        request_id request,
        const server_response& result);

private:
    struct subscription_entry final {
        subscription_id id;
        subscription_registration registration;
    };

    communication_connection(
        client_session_id id,
        std::size_t outbound_byte_limit);

    ~communication_connection() = default;

    void retain() noexcept;
    void release() noexcept;

    [[nodiscard]] static std::size_t estimate_response_bytes(
        const server_response& result) noexcept;

    friend class connection_lifetime_token;
    friend class communication_connection_owner;

    std::atomic_size_t references{1};

    client_session session;
    std::atomic_bool login_committed = false;
    std::atomic_bool close_started = false;

    outbound_channel outbound;

    mutable std::mutex request_mutex;
    std::unordered_set<std::uint64_t> outstanding_requests;

    // Mutated only by Server control thread.
    std::vector<subscription_entry> subscriptions;
};

}
