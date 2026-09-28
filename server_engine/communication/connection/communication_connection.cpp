#include "communication_connection.hpp"

#include <algorithm>
#include <utility>

namespace cw::server {
namespace {

[[nodiscard]] std::size_t diagnostic_bytes(
    const diagnostic_collection& diagnostics) noexcept {

    std::size_t result = 0;

    for (const auto& record : diagnostics.records()) {
        result +=
            record.detail.size() +
            record.message.size() +
            64;
    }

    return result;
}

}

communication_connection_owner::communication_connection_owner(
    communication_connection* value) noexcept
    : connection(value) {
}

communication_connection_owner::communication_connection_owner(
    communication_connection_owner&& other) noexcept
    : connection(std::exchange(other.connection, nullptr)) {
}

communication_connection_owner&
communication_connection_owner::operator=(
    communication_connection_owner&& other) noexcept {

    if (this != &other) {
        reset();
        connection =
            std::exchange(other.connection, nullptr);
    }

    return *this;
}

communication_connection_owner::~communication_connection_owner() {
    reset();
}

void communication_connection_owner::reset() noexcept {
    if (connection != nullptr) {
        auto* value = connection;
        connection = nullptr;
        value->release();
    }
}

communication_connection*
communication_connection_owner::get() const noexcept {
    return connection;
}

communication_connection&
communication_connection_owner::operator*() const noexcept {
    return *connection;
}

communication_connection*
communication_connection_owner::operator->() const noexcept {
    return connection;
}

communication_connection_owner::operator bool() const noexcept {
    return connection != nullptr;
}

communication_connection_owner
communication_connection::create(
    client_session_id id,
    std::size_t outbound_byte_limit) {

    return communication_connection_owner{
        new communication_connection{
            id,
            outbound_byte_limit,
        },
    };
}

communication_connection::communication_connection(
    client_session_id id,
    std::size_t outbound_byte_limit)
    : session(id),
      outbound(outbound_byte_limit) {
}

client_session_id communication_connection::id() const noexcept {
    return session.id();
}

bool communication_connection::logged_in() const noexcept {
    return login_committed.load(
        std::memory_order_acquire);
}

std::string_view communication_connection::login_name() const noexcept {
    return session.name();
}

client_session_login_result communication_connection::commit_login(
    std::string_view name) noexcept {

    if (closing()) {
        return client_session_login_result::invalid_request;
    }

    const auto result =
        session.login(name);

    if (result == client_session_login_result::success) {
        login_committed.store(
            true,
            std::memory_order_release);
    }

    return result;
}

bool communication_connection::reserve_request(
    request_id request) {

    if (!request.valid() || closing()) {
        return false;
    }

    std::lock_guard lock(request_mutex);

    if (closing()) {
        return false;
    }

    return outstanding_requests
        .insert(request.value)
        .second;
}

void communication_connection::release_request(
    request_id request) noexcept {

    if (!request.valid()) {
        return;
    }

    std::lock_guard lock(request_mutex);
    outstanding_requests.erase(request.value);
}

bool communication_connection::request_outstanding(
    request_id request) const {

    if (!request.valid()) {
        return false;
    }

    std::lock_guard lock(request_mutex);

    return outstanding_requests.find(request.value) !=
        outstanding_requests.end();
}

std::size_t communication_connection::estimate_response_bytes(
    const server_response& result) noexcept {

    return 96 + diagnostic_bytes(result.diagnostics);
}

bool communication_connection::enqueue_response(
    request_id request,
    const server_response& result) {

    outbound_message message;
    message.estimated_bytes =
        estimate_response_bytes(result);

    message.payload =
        protocol_response{
            request,
            result,
        };

    if (!outbound.enqueue(std::move(message))) {
        (void)begin_close();
        return false;
    }

    return true;
}

bool communication_connection::enqueue_server_state(
    project_state old_state,
    project_state new_state) {

    if (!logged_in()) {
        return false;
    }

    outbound_message message;
    message.estimated_bytes = 64;
    message.payload =
        server_state_action{
            old_state,
            new_state,
        };

    if (!outbound.enqueue(std::move(message))) {
        (void)begin_close();
        return false;
    }

    return true;
}

bool communication_connection::enqueue_client_action(
    client_action action) {

    if (!logged_in()) {
        return false;
    }

    outbound_message message;
    message.estimated_bytes =
        64 +
        action.from.size() +
        action.arg.size() +
        action.parameters.size();

    message.payload = std::move(action);

    if (!outbound.enqueue(std::move(message))) {
        (void)begin_close();
        return false;
    }

    return true;
}

bool communication_connection::enqueue_subscription_data(
    subscription_data data) {

    if (!logged_in()) {
        return false;
    }

    outbound_message message;
    message.estimated_bytes =
        64 + data.payload.size();

    message.payload = std::move(data);

    if (!outbound.enqueue(std::move(message))) {
        (void)begin_close();
        return false;
    }

    return true;
}

bool communication_connection::wait_next(
    outbound_write& output) {

    if (!outbound.wait_next(output)) {
        return false;
    }

    if (const auto* response =
            std::get_if<protocol_response>(
                &output.message.payload)) {

        release_request(response->request);
    }

    return true;
}

void communication_connection::mark_written(
    std::uint64_t connection_sequence) noexcept {

    outbound.mark_written(connection_sequence);
}

bool communication_connection::wait_until_written(
    std::uint64_t connection_sequence,
    std::chrono::milliseconds timeout) {

    return outbound.wait_until_written(
        connection_sequence,
        timeout);
}

bool communication_connection::begin_close() noexcept {
    const auto first =
        !close_started.exchange(
            true,
            std::memory_order_acq_rel);

    (void)outbound.close();
    return first;
}

bool communication_connection::closing() const noexcept {
    return close_started.load(
        std::memory_order_acquire);
}

bool communication_connection::add_subscription(
    subscription_id id,
    subscription_registration registration) {

    if (!id.valid() || closing()) {
        return false;
    }

    const auto duplicate =
        std::find_if(
            subscriptions.begin(),
            subscriptions.end(),
            [id](const subscription_entry& entry) {
                return entry.id == id;
            });

    if (duplicate != subscriptions.end()) {
        return false;
    }

    subscriptions.push_back(
        subscription_entry{
            id,
            std::move(registration),
        });

    return true;
}

bool communication_connection::remove_subscription(
    subscription_id id) noexcept {

    const auto position =
        std::find_if(
            subscriptions.begin(),
            subscriptions.end(),
            [id](const subscription_entry& entry) {
                return entry.id == id;
            });

    if (position == subscriptions.end()) {
        return false;
    }

    subscriptions.erase(position);
    return true;
}

void communication_connection::teardown_subscriptions() noexcept {
    subscriptions.clear();
}

std::size_t communication_connection::subscription_count() const noexcept {
    return subscriptions.size();
}

connection_lifetime_token communication_connection::hold() noexcept {
    retain();

    return connection_lifetime_token{
        this,
        [](void* context) noexcept {
            static_cast<communication_connection*>(
                context)->release();
        },
    };
}

void communication_connection::present_response(
    void* context,
    request_id request,
    const server_response& result) {

    auto& connection =
        *static_cast<communication_connection*>(context);

    (void)connection.enqueue_response(
        request,
        result);
}

void communication_connection::retain() noexcept {
    references.fetch_add(
        1,
        std::memory_order_relaxed);
}

void communication_connection::release() noexcept {
    if (references.fetch_sub(
            1,
            std::memory_order_acq_rel) == 1) {

        delete this;
    }
}

}
