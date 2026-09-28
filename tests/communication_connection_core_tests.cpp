#include "communication/connection/communication_connection.hpp"
#include "communication/server_request_message.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <variant>

using namespace cw::server;

namespace {

void unregister_subscription(void* context) noexcept {
    static_cast<std::atomic_uint32_t*>(context)
        ->fetch_add(1, std::memory_order_relaxed);
}

}

int main() {
    auto owner =
        communication_connection::create(
            client_session_id{1},
            1024);

    if (!owner ||
        owner->logged_in() ||
        owner->closing()) {

        return 1;
    }

    if (!owner->reserve_request(request_id{10}) ||
        owner->reserve_request(request_id{10})) {

        return 2;
    }

    if (owner->commit_login("Studio") !=
            client_session_login_result::success ||
        !owner->logged_in() ||
        owner->login_name() != "Studio") {

        return 3;
    }

    server_response response;
    response.status = server_status::success;

    if (!owner->enqueue_response(
            request_id{10},
            response)) {

        return 4;
    }

    outbound_write first;

    std::jthread writer([&] {
        if (!owner->wait_next(first)) {
            return;
        }

        owner->mark_written(
            first.connection_sequence);
    });

    writer.join();

    if (first.connection_sequence != 1 ||
        owner->request_outstanding(request_id{10})) {

        return 5;
    }

    if (!owner->wait_until_written(
            1,
            std::chrono::milliseconds{100})) {

        return 6;
    }

    if (!owner->reserve_request(request_id{10})) {
        return 7;
    }

    if (!owner->enqueue_server_state(
            project_state::unloaded,
            project_state::loaded)) {

        return 8;
    }

    outbound_write state_write;

    std::jthread state_writer([&] {
        if (!owner->wait_next(state_write)) {
            return;
        }

        owner->mark_written(
            state_write.connection_sequence);
    });

    state_writer.join();

    if (state_write.connection_sequence != 2 ||
        !std::holds_alternative<server_state_action>(
            state_write.message.payload)) {

        return 9;
    }

    std::atomic_uint32_t unregister_count = 0;

    if (!owner->add_subscription(
            subscription_id{5},
            subscription_registration{
                &unregister_count,
                &unregister_subscription,
            })) {

        return 10;
    }

    owner->teardown_subscriptions();

    if (owner->subscription_count() != 0 ||
        unregister_count.load(
            std::memory_order_relaxed) != 1) {

        return 11;
    }

    auto lifetime =
        owner->hold();

    server_request_origin origin{
        owner.get(),
        &communication_connection::present_response,
        request_id{10},
        std::move(lifetime),
    };

    owner =
        communication_connection_owner{};

    server_response late;
    late.status = server_status::success;
    origin.present(late);

    auto bounded =
        communication_connection::create(
            client_session_id{2},
            64);

    if (bounded->commit_login("HMI") !=
        client_session_login_result::success) {

        return 12;
    }

    client_action too_large;
    too_large.from = "Studio";
    too_large.arg = "x";
    too_large.parameters.resize(128);

    if (bounded->enqueue_client_action(
            std::move(too_large)) ||
        !bounded->closing()) {

        return 13;
    }

    client_action after_close;
    after_close.from = "Studio";

    if (bounded->enqueue_client_action(
            std::move(after_close))) {

        return 14;
    }

    return 0;
}
