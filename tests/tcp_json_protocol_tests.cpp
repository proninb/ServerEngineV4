#include "communication/protocol/json_protocol.hpp"

#include <cstddef>
#include <string>
#include <variant>

using namespace cw::server;

int main() {
    {
        json_request request;

        const auto decoded =
            decode_json_request(
                R"({"request_id":1,"command":"LOGIN","arguments":{"name":"Studio"}})",
                request);

        if (!decoded.ok() ||
            request.request != request_id{1} ||
            request.kind !=
                json_request_kind::login ||
            std::get<json_login_request>(
                request.payload).name !=
                "Studio") {

            return 1;
        }
    }

    {
        json_request request;

        const auto decoded =
            decode_json_request(
                R"({"request_id":2,"command":"LOAD","arguments":{"path":"project.json"}})",
                request);

        if (!decoded.ok() ||
            request.kind !=
                json_request_kind::server ||
            std::get<server_request>(
                request.payload).kind !=
                server_request_kind::load) {

            return 2;
        }
    }

    {
        json_request request;

        const auto decoded =
            decode_json_request(
                R"({"request_id":30,"command":"SNAP_IC","arguments":{"path":"snap.ic"}})",
                request);

        if (!decoded.ok() ||
            request.kind !=
                json_request_kind::server) {

            return 30;
        }

        const auto& server =
            std::get<server_request>(
                request.payload);

        if (server.kind !=
                server_request_kind::snap_ic ||
            server.path !=
                "snap.ic") {

            return 31;
        }
    }

    {
        json_request request;

        const auto decoded =
            decode_json_request(
                R"({"request_id":32,"command":"RESET_IC","arguments":{"path":"snap.ic"}})",
                request);

        if (!decoded.ok() ||
            request.kind !=
                json_request_kind::server) {

            return 32;
        }

        const auto& server =
            std::get<server_request>(
                request.payload);

        if (server.kind !=
                server_request_kind::reset_ic ||
            server.path !=
                "snap.ic" ||
            server.reset_options !=
                reset_ic_options::none) {

            return 33;
        }
    }

    {
        json_request request;

        const auto decoded =
            decode_json_request(
                R"({"request_id":34,"command":"RESET_IC","arguments":{"path":"snap.ic","options":3}})",
                request);

        if (!decoded.ok() ||
            request.kind !=
                json_request_kind::server) {

            return 34;
        }

        const auto& server =
            std::get<server_request>(
                request.payload);

        if (server.kind !=
                server_request_kind::reset_ic ||
            server.path !=
                "snap.ic" ||
            server.reset_options !=
                (reset_ic_options::constants |
                 reset_ic_options::variables)) {

            return 35;
        }
    }

    {
        json_request request;

        const auto decoded =
            decode_json_request(
                R"({"request_id":36,"command":"RESET_IC","arguments":{"path":"snap.ic","options":7}})",
                request);

        if (!decoded.ok()) {
            return 36;
        }

        const auto& server =
            std::get<server_request>(
                request.payload);

        if (server.reset_options !=
                (reset_ic_options::constants |
                 reset_ic_options::variables |
                 reset_ic_options::defaults)) {

            return 37;
        }
    }

    {
        json_request request;

        if (decode_json_request(
                R"({"request_id":38,"command":"RESET_IC","arguments":{"path":"snap.ic","options":8}})",
                request).ok()) {

            return 38;
        }
    }

    {
        json_request request;

        if (decode_json_request(
                R"({"request_id":39,"command":"SNAP_IC","arguments":{"path":"snap.ic","options":1}})",
                request).ok()) {

            return 39;
        }
    }

    {
        json_request request;

        if (decode_json_request(
                R"({"request_id":40,"command":"LOAD","arguments":{"path":"project.json","options":1}})",
                request).ok()) {

            return 40;
        }
    }

    {
        json_request request;

        const auto decoded =
            decode_json_request(
                R"({"request_id":3,"command":"CLIENT","arguments":{"login":"","arg":"ping","parameters":"AQID"}})",
                request);

        if (!decoded.ok()) {
            return 3;
        }

        const auto& message =
            std::get<json_client_request>(
                request.payload).message;

        if (message.arg != "ping" ||
            message.parameters.size() != 3 ||
            message.parameters[0] !=
                std::byte{1} ||
            message.parameters[1] !=
                std::byte{2} ||
            message.parameters[2] !=
                std::byte{3}) {

            return 4;
        }
    }

    {
        // Strict schema: only "arguments" may introduce the nested object.
        json_request request;

        const auto decoded =
            decode_json_request(
                R"({"request_id":4,"command":"GET_STATE","unknown":{}})",
                request);

        if (decoded.ok()) {
            return 5;
        }
    }

    {
        // Strict schema: a second arguments object is rejected.
        json_request request;

        const auto decoded =
            decode_json_request(
                R"({"request_id":5,"command":"GET_STATE","arguments":{},"arguments":{}})",
                request);

        if (decoded.ok()) {
            return 6;
        }
    }

    {
        outbound_write message;
        message.connection_sequence = 7;

        server_response response;
        response.status =
            server_status::success;
        response.payload =
            server_response_payload_kind::state;
        response.state.project =
            project_state::loaded;

        message.message.payload =
            protocol_response{
                request_id{9},
                response,
            };

        std::string json;

        if (!encode_json_outbound(
                message,
                json) ||
            json.find(
                R"("sequence":7)") ==
                std::string::npos ||
            json.find(
                R"("request_id":9)") ==
                std::string::npos ||
            json.find(
                R"("state":"LOADED")") ==
                std::string::npos) {

            return 5;
        }

        std::string frame;
        encode_tcp_frame(
            json,
            frame);

        if (frame.size() !=
                json.size() + 4 ||
            decode_tcp_frame_length(
                reinterpret_cast<
                    const std::byte*>(
                    frame.data())) !=
                json.size()) {

            return 6;
        }
    }

    return 0;
}
