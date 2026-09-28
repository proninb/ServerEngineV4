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

    return 0;
}
