#include "communication/connection/communication_connection.hpp"
#include "communication/control/communication_control.hpp"
#include "communication/request_queue.hpp"

#include <chrono>
#include <variant>

using namespace cw::server;

int main() {
    request_queue queue;

    auto connection =
        communication_connection::create(
            client_session_id{100},
            4096);

    if (!connection->reserve_request(request_id{1})) {
        return 1;
    }

    login_control_message login;
    login.connection = connection.get();
    login.lifetime = connection->hold();
    login.request = request_id{1};
    login.name = "Studio";

    if (!queue.push(
            communication_control_message{
                std::move(login)})) {

        return 2;
    }

    communication_control_message output;

    if (!queue.wait_pop_until(
            std::chrono::system_clock::now() +
                std::chrono::seconds{1},
            output)) {

        return 3;
    }

    auto* received =
        std::get_if<login_control_message>(
            &output);

    if (received == nullptr ||
        received->connection != connection.get() ||
        received->request != request_id{1} ||
        received->name != "Studio") {

        return 4;
    }

    queue.stop_accepting_and_discard();

    client_control_message rejected;
    rejected.connection = connection.get();
    rejected.lifetime = connection->hold();
    rejected.request = request_id{2};

    if (queue.push(
            communication_control_message{
                std::move(rejected)})) {

        return 5;
    }

    communication_control_message stopped_output;

    if (queue.wait_pop(stopped_output)) {
        return 6;
    }

    request_queue bounded{
        request_queue_limits{
            1,
            1024u * 1024u,
        }};

    server_request_message first;
    first.request.kind =
        server_request_kind::get_state;

    if (bounded.try_push(
            communication_control_message{
                std::move(first)}) !=
            request_queue_push_result::
                accepted) {

        return 7;
    }

    server_request_message second;
    second.request.kind =
        server_request_kind::get_state;

    if (bounded.try_push(
            communication_control_message{
                std::move(second)}) !=
            request_queue_push_result::
                overloaded) {

        return 8;
    }

    communication_control_message bounded_output;

    if (!bounded.wait_pop(
            bounded_output)) {

        return 9;
    }

    server_request_message after_pop;
    after_pop.request.kind =
        server_request_kind::get_state;

    if (bounded.try_push(
            communication_control_message{
                std::move(after_pop)}) !=
            request_queue_push_result::
                accepted) {

        return 10;
    }

    connection_close_control_message close;

    if (bounded.try_push(
            communication_control_message{
                std::move(close)}) !=
            request_queue_push_result::
                accepted) {

        return 11;
    }

    if (!bounded.wait_pop(
            bounded_output) ||
        !bounded.wait_pop(
            bounded_output) ||
        !std::holds_alternative<
            connection_close_control_message>(
                bounded_output)) {

        return 12;
    }

    request_queue byte_bounded{
        request_queue_limits{
            8,
            sizeof(
                communication_control_message) +
                8,
        }};

    server_request_message large;
    large.request.kind =
        server_request_kind::get_value;
    large.request.name.assign(
        128,
        'x');

    if (byte_bounded.try_push(
            communication_control_message{
                std::move(large)}) !=
            request_queue_push_result::
                overloaded) {

        return 13;
    }

    byte_bounded.stop_accepting_and_discard();

    server_request_message stopped;
    stopped.request.kind =
        server_request_kind::get_state;

    if (byte_bounded.try_push(
            communication_control_message{
                std::move(stopped)}) !=
            request_queue_push_result::
                stopped) {

        return 14;
    }

    return 0;
}
