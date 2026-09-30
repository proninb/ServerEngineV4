#include "json_protocol.hpp"

#include "../../json/json_parser.hpp"
#include "../../filesystem_path.hpp"

#include <array>
#include <charconv>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

namespace cw::server {
namespace {

enum field_bit : std::uint32_t {
    request_id_bit = 1u << 0,
    command_bit = 1u << 1,
    name_bit = 1u << 2,
    path_bit = 1u << 3,
    login_bit = 1u << 4,
    arg_bit = 1u << 5,
    parameters_bit = 1u << 6,
    options_bit = 1u << 7,
};

class request_handler final : public json_event_handler {
public:
    void location(std::size_t, std::size_t) override {
    }

    void object_begin() override {
        if (depth == 0) {
            depth = 1;
            return;
        }

        if (depth == 1) {
            if (key_value != "arguments" || arguments_seen) {
                valid_value = false;
            }

            arguments_seen = true;
            depth = 2;
            return;
        }

        ++depth;
        valid_value = false;
    }

    void object_end() override {
        if (depth == 0) {
            valid_value = false;
            return;
        }

        --depth;
    }

    void array_begin() override {
        valid_value = false;
        ++depth;
    }

    void array_end() override {
        if (depth == 0) {
            valid_value = false;
            return;
        }

        --depth;
    }

    void key(std::string_view value) override {
        key_value.assign(value.data(), value.size());
    }

    void value(json_value_view value) override {
        if (!valid_value) {
            return;
        }

        if (depth != 1 && depth != 2) {
            valid_value = false;
            return;
        }

        if (depth == 1) {
            if (key_value == "request_id") {
                if (!mark(request_id_bit) || !value.get(request_value)) {
                    valid_value = false;
                }
                return;
            }

            if (key_value == "command") {
                if (!mark(command_bit) || !value.get(command_value)) {
                    valid_value = false;
                }
                return;
            }

            valid_value = false;
            return;
        }

        if (key_value == "name") {
            assign_string(name_bit, value, name_value);
        } else if (key_value == "path") {
            assign_string(path_bit, value, path_value);
        } else if (key_value == "login") {
            assign_string(login_bit, value, login_value);
        } else if (key_value == "arg") {
            assign_string(arg_bit, value, arg_value);
        } else if (key_value == "parameters") {
            assign_string(parameters_bit, value, parameters_value);
        } else if (key_value == "options") {
            if (!mark(options_bit) || !value.get(options_value)) {
                valid_value = false;
            }
        } else {
            valid_value = false;
        }
    }

    [[nodiscard]] bool valid() const noexcept {
        return valid_value &&
            depth == 0 &&
            (seen & request_id_bit) != 0 &&
            (seen & command_bit) != 0 &&
            request_value != 0;
    }

    std::uint64_t request_value = 0;
    std::string command_value;
    std::string name_value;
    std::string path_value;
    std::string login_value;
    std::string arg_value;
    std::string parameters_value;
    std::uint64_t options_value = 0;
    std::uint32_t seen = 0;

private:
    [[nodiscard]] bool mark(std::uint32_t bit) noexcept {
        if ((seen & bit) != 0) {
            return false;
        }
        seen |= bit;
        return true;
    }

    void assign_string(
        std::uint32_t bit,
        json_value_view value,
        std::string& output) {

        if (!mark(bit) || !value.get(output)) {
            valid_value = false;
        }
    }

    std::size_t depth = 0;
    bool valid_value = true;
    bool arguments_seen = false;
    std::string key_value;
};

[[nodiscard]] bool only(
    std::uint32_t seen,
    std::uint32_t allowed) noexcept {

    constexpr auto common =
        request_id_bit |
        command_bit;

    return (seen & ~(common | allowed)) == 0;
}

[[nodiscard]] int base64_value(
    char value) noexcept {

    if (value >= 'A' && value <= 'Z') {
        return value - 'A';
    }

    if (value >= 'a' && value <= 'z') {
        return value - 'a' + 26;
    }

    if (value >= '0' && value <= '9') {
        return value - '0' + 52;
    }

    if (value == '+') {
        return 62;
    }

    if (value == '/') {
        return 63;
    }

    return -1;
}

[[nodiscard]] bool decode_base64(
    std::string_view input,
    std::vector<std::byte>& output) {

    output.clear();

    if (input.empty()) {
        return true;
    }

    if ((input.size() % 4u) != 0) {
        return false;
    }

    output.reserve(
        (input.size() / 4u) * 3u);

    for (std::size_t offset = 0;
         offset < input.size();
         offset += 4) {

        const bool last =
            offset + 4 == input.size();

        const char c0 = input[offset];
        const char c1 = input[offset + 1];
        const char c2 = input[offset + 2];
        const char c3 = input[offset + 3];

        const int v0 = base64_value(c0);
        const int v1 = base64_value(c1);

        if (v0 < 0 || v1 < 0) {
            return false;
        }

        const bool pad2 = c2 == '=';
        const bool pad3 = c3 == '=';

        if (pad2 && !pad3) {
            return false;
        }

        if ((pad2 || pad3) && !last) {
            return false;
        }

        const int v2 =
            pad2 ? 0 : base64_value(c2);

        const int v3 =
            pad3 ? 0 : base64_value(c3);

        if (v2 < 0 || v3 < 0) {
            return false;
        }

        const auto bits =
            (static_cast<std::uint32_t>(v0) << 18u) |
            (static_cast<std::uint32_t>(v1) << 12u) |
            (static_cast<std::uint32_t>(v2) << 6u) |
            static_cast<std::uint32_t>(v3);

        output.push_back(
            static_cast<std::byte>(
                (bits >> 16u) & 0xFFu));

        if (!pad2) {
            output.push_back(
                static_cast<std::byte>(
                    (bits >> 8u) & 0xFFu));
        }

        if (!pad3) {
            output.push_back(
                static_cast<std::byte>(
                    bits & 0xFFu));
        }

        if (pad2 && (v1 & 0x0F) != 0) {
            return false;
        }

        if (pad3 && !pad2 &&
            (v2 & 0x03) != 0) {
            return false;
        }
    }

    return true;
}

void append_base64(
    const std::vector<std::byte>& input,
    std::string& output) {

    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    for (std::size_t offset = 0;
         offset < input.size();
         offset += 3) {

        const auto remaining =
            input.size() - offset;

        const auto a =
            std::to_integer<std::uint32_t>(
                input[offset]);

        const auto b =
            remaining > 1
                ? std::to_integer<std::uint32_t>(
                      input[offset + 1])
                : 0u;

        const auto c =
            remaining > 2
                ? std::to_integer<std::uint32_t>(
                      input[offset + 2])
                : 0u;

        const auto bits =
            (a << 16u) |
            (b << 8u) |
            c;

        output.push_back(
            alphabet[(bits >> 18u) & 0x3Fu]);

        output.push_back(
            alphabet[(bits >> 12u) & 0x3Fu]);

        output.push_back(
            remaining > 1
                ? alphabet[(bits >> 6u) & 0x3Fu]
                : '=');

        output.push_back(
            remaining > 2
                ? alphabet[bits & 0x3Fu]
                : '=');
    }
}

void append_escaped(
    std::string_view value,
    std::string& output) {

    static constexpr char hex[] =
        "0123456789ABCDEF";

    output.push_back('"');

    for (const auto ch : value) {
        const auto byte =
            static_cast<unsigned char>(ch);

        switch (ch) {
        case '"':
            output += "\\\"";
            break;
        case '\\':
            output += "\\\\";
            break;
        case '\b':
            output += "\\b";
            break;
        case '\f':
            output += "\\f";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            if (byte < 0x20u) {
                output += "\\u00";
                output.push_back(
                    hex[(byte >> 4u) & 0x0Fu]);
                output.push_back(
                    hex[byte & 0x0Fu]);
            } else {
                output.push_back(ch);
            }
            break;
        }
    }

    output.push_back('"');
}

template <typename T>
void append_integer(
    T value,
    std::string& output) {

    std::array<char, 32> buffer{};

    const auto converted =
        std::to_chars(
            buffer.data(),
            buffer.data() +
                buffer.size(),
            value);

    if (converted.ec == std::errc{}) {
        output.append(
            buffer.data(),
            converted.ptr);
    }
}

[[nodiscard]] const char* state_name(
    project_state state) noexcept {

    switch (state) {
    case project_state::unloaded:
        return "UNLOADED";
    case project_state::loaded:
        return "LOADED";
    case project_state::run:
        return "RUN";
    case project_state::freeze:
        return "FREEZE";
    case project_state::resetting_ic:
        return "RESETTING_IC";
    case project_state::snapping_ic:
        return "SNAPPING_IC";
    }

    return "UNLOADED";
}

void append_diagnostics(
    const diagnostic_collection& diagnostics,
    std::string& output) {

    if (diagnostics.empty()) {
        return;
    }

    output += ",\"diagnostics\":[";

    bool first = true;

    for (const auto& record :
         diagnostics.records()) {

        if (!first) {
            output.push_back(',');
        }

        first = false;
        output += "{\"id\":";
        append_integer(
            record.id.value(),
            output);

        output += ",\"severity\":";
        append_escaped(
            diagnostic_severity_name(
                record.severity),
            output);

        if (!record.message.empty()) {
            output += ",\"message\":";
            append_escaped(
                record.message,
                output);
        }

        if (!record.detail.empty()) {
            output += ",\"detail\":";
            append_escaped(
                record.detail,
                output);
        }

        if (record.location.has_position()) {
            output += ",\"line\":";
            append_integer(
                record.location.line,
                output);

            output += ",\"column\":";
            append_integer(
                record.location.column,
                output);
        }

        output.push_back('}');
    }

    output.push_back(']');
}

void append_ic_catalog_entry(
    const ic_catalog_entry& entry,
    std::string& output) {

    std::string path_text;
    (void)filesystem_path_to_utf8(
        entry.path,
        path_text);

    output += "{\"description\":";
    append_escaped(
        entry.description,
        output);

    output += ",\"size\":";
    append_integer(
        entry.size,
        output);

    output += ",\"path\":";
    append_escaped(
        path_text,
        output);

    output.push_back('}');
}

void append_response(
    std::uint64_t sequence,
    const protocol_response& response,
    std::string& output) {

    output += "{\"sequence\":";
    append_integer(sequence, output);

    output += ",\"request_id\":";
    append_integer(
        response.request.value,
        output);

    output += ",\"status\":";
    output += succeeded(
        response.result.status)
        ? "true"
        : "false";

    switch (response.result.payload) {
    case server_response_payload_kind::none:
        break;

    case server_response_payload_kind::state:
        output += ",\"payload\":{\"state\":";
        append_escaped(
            state_name(
                response.result.state.project),
            output);
        output.push_back('}');
        break;

    case server_response_payload_kind::runtime_value:
        output += ",\"payload\":{\"type\":";
        append_integer(
            static_cast<std::underlying_type_t<intrinsic_type>>(
                response.result.value.type),
            output);

        output += ",\"size\":";
        append_integer(
            response.result.value.size,
            output);

        output += ",\"bits\":";
        append_integer(
            response.result.value.bits,
            output);

        output.push_back('}');
        break;

    case server_response_payload_kind::ic_catalog:
        output += ",\"payload\":{\"items\":[";

        for (std::size_t index = 0;
             index < response.result.catalog.items.size();
             ++index) {

            if (index != 0) {
                output.push_back(',');
            }

            append_ic_catalog_entry(
                response.result.catalog.items[index],
                output);
        }

        output += "]}";
        break;
    }

    append_diagnostics(
        response.result.diagnostics,
        output);

    output.push_back('}');
}

void append_server_state(
    std::uint64_t sequence,
    const server_state_action& action,
    std::string& output) {

    output += "{\"sequence\":";
    append_integer(sequence, output);

    output +=
        ",\"action\":\"SERVER_STATE\",\"old\":";

    append_escaped(
        state_name(action.old_state),
        output);

    output += ",\"new\":";

    append_escaped(
        state_name(action.new_state),
        output);

    output.push_back('}');
}

void append_client_action(
    std::uint64_t sequence,
    const client_action& action,
    std::string& output) {

    output += "{\"sequence\":";
    append_integer(sequence, output);

    output +=
        ",\"action\":\"CLIENT\",\"from\":";

    append_escaped(
        action.from,
        output);

    output += ",\"arg\":";
    append_escaped(
        action.arg,
        output);

    output += ",\"parameters\":\"";
    append_base64(
        action.parameters,
        output);
    output += "\"}";
}

void append_subscription_data(
    std::uint64_t sequence,
    const subscription_data& data,
    std::string& output) {

    output += "{\"sequence\":";
    append_integer(sequence, output);

    output +=
        ",\"action\":\"SUBSCRIPTION_DATA\","
        "\"subscription_id\":";

    append_integer(
        data.subscription.value,
        output);

    output += ",\"subscription_sequence\":";
    append_integer(
        data.sequence.value,
        output);

    output += ",\"parameters\":\"";
    append_base64(
        data.payload,
        output);
    output += "\"}";
}

}

json_decode_result decode_json_request(
    std::string_view text,
    json_request& output) noexcept {

    try {
        request_handler handler;

        const auto parsed =
            parse_json(
                text,
                handler);

        if (!parsed.ok()) {
            return {
                json_protocol_error::invalid_json,
            };
        }

        if (!handler.valid()) {
            return {
                json_protocol_error::invalid_schema,
            };
        }

        json_request candidate;
        candidate.request =
            request_id{
                handler.request_value,
            };

        const auto command =
            std::string_view(
                handler.command_value);

        if (command == "LOGIN") {
            if (!only(
                    handler.seen,
                    name_bit) ||
                (handler.seen & name_bit) == 0) {

                return {
                    json_protocol_error::invalid_schema,
                };
            }

            candidate.kind =
                json_request_kind::login;

            candidate.payload =
                json_login_request{
                    std::move(
                        handler.name_value),
                };
        } else if (command == "CLIENT") {
            constexpr auto allowed =
                login_bit |
                arg_bit |
                parameters_bit;

            if (!only(
                    handler.seen,
                    allowed)) {

                return {
                    json_protocol_error::invalid_schema,
                };
            }

            json_client_request client;
            client.message.login =
                std::move(
                    handler.login_value);

            client.message.arg =
                std::move(
                    handler.arg_value);

            if ((handler.seen &
                 parameters_bit) != 0 &&
                !decode_base64(
                    handler.parameters_value,
                    client.message.parameters)) {

                return {
                    json_protocol_error::invalid_schema,
                };
            }

            candidate.kind =
                json_request_kind::client;

            candidate.payload =
                std::move(client);
        } else {
            server_request request;

            if (command == "LOAD") {
                request.kind =
                    server_request_kind::load;
            } else if (command == "PUBLISH") {
                request.kind =
                    server_request_kind::publish;
            } else if (command == "BUILD") {
                request.kind =
                    server_request_kind::build;
            } else if (command == "UNLOAD") {
                request.kind =
                    server_request_kind::unload;
            } else if (command == "REBUILD") {
                request.kind =
                    server_request_kind::rebuild;
            } else if (command == "GET_STATE") {
                request.kind =
                    server_request_kind::get_state;
            } else if (command == "GET_VALUE") {
                request.kind =
                    server_request_kind::get_value;
            } else if (command == "SNAP_IC") {
                request.kind =
                    server_request_kind::snap_ic;
            } else if (command == "RESET_IC") {
                request.kind =
                    server_request_kind::reset_ic;
            } else if (command == "DELETE_IC") {
                request.kind =
                    server_request_kind::delete_ic;
            } else if (command == "LIST_IC") {
                request.kind =
                    server_request_kind::list_ic;
            } else if (command == "RUN") {
                request.kind =
                    server_request_kind::run;
            } else if (command == "FREEZE") {
                request.kind =
                    server_request_kind::freeze;
            } else if (command == "SHUTDOWN") {
                request.kind =
                    server_request_kind::shutdown;
            } else {
                return {
                    json_protocol_error::unsupported_command,
                };
            }

            switch (request.kind) {
            case server_request_kind::load:
            case server_request_kind::publish:
            case server_request_kind::build:
            case server_request_kind::rebuild:
                if (!only(
                        handler.seen,
                        path_bit) ||
                    (handler.seen & path_bit) == 0 ||
                    handler.path_value.empty()) {

                    return {
                        json_protocol_error::invalid_schema,
                    };
                }

                request.path =
                    std::move(
                        handler.path_value);
                break;

            case server_request_kind::snap_ic:
                if (!only(
                        handler.seen,
                        path_bit |
                            options_bit) ||
                    (handler.seen & path_bit) == 0 ||
                    handler.path_value.empty() ||
                    handler.options_value != 0) {

                    return {
                        json_protocol_error::invalid_schema,
                    };
                }

                request.path =
                    std::move(
                        handler.path_value);

                request.snap_options =
                    static_cast<snap_ic_options>(
                        handler.options_value);
                break;

            case server_request_kind::reset_ic:
                if (!only(
                        handler.seen,
                        path_bit |
                            options_bit) ||
                    (handler.seen & path_bit) == 0 ||
                    handler.path_value.empty() ||
                    handler.options_value >
                        reset_ic_options_mask) {

                    return {
                        json_protocol_error::invalid_schema,
                    };
                }

                request.path =
                    std::move(
                        handler.path_value);

                request.reset_options =
                    static_cast<reset_ic_options>(
                        handler.options_value);
                break;

            case server_request_kind::delete_ic:
                if (!only(
                        handler.seen,
                        path_bit) ||
                    (handler.seen & path_bit) == 0 ||
                    handler.path_value.empty()) {

                    return {
                        json_protocol_error::invalid_schema,
                    };
                }

                request.path =
                    std::move(
                        handler.path_value);
                break;

            case server_request_kind::get_value:
                if (!only(
                        handler.seen,
                        name_bit) ||
                    (handler.seen & name_bit) == 0 ||
                    handler.name_value.empty()) {

                    return {
                        json_protocol_error::invalid_schema,
                    };
                }

                request.name =
                    std::move(
                        handler.name_value);
                break;

            case server_request_kind::unload:
            case server_request_kind::get_state:
            case server_request_kind::list_ic:
            case server_request_kind::run:
            case server_request_kind::freeze:
            case server_request_kind::shutdown:
                if (!only(
                        handler.seen,
                        0)) {

                    return {
                        json_protocol_error::invalid_schema,
                    };
                }
                break;
            }

            candidate.kind =
                json_request_kind::server;

            candidate.payload =
                std::move(request);
        }

        output =
            std::move(candidate);

        return {};
    } catch (...) {
        return {
            json_protocol_error::invalid_schema,
        };
    }
}

bool encode_json_outbound(
    const outbound_write& message,
    std::string& output) noexcept {

    try {
        output.clear();
        output.reserve(
            message.message.estimated_bytes);

        if (const auto* response =
                std::get_if<protocol_response>(
                    &message.message.payload)) {

            append_response(
                message.connection_sequence,
                *response,
                output);
        } else if (
            const auto* state =
                std::get_if<server_state_action>(
                    &message.message.payload)) {

            append_server_state(
                message.connection_sequence,
                *state,
                output);
        } else if (
            const auto* client =
                std::get_if<client_action>(
                    &message.message.payload)) {

            append_client_action(
                message.connection_sequence,
                *client,
                output);
        } else if (
            const auto* data =
                std::get_if<subscription_data>(
                    &message.message.payload)) {

            append_subscription_data(
                message.connection_sequence,
                *data,
                output);
        } else {
            return false;
        }

        return output.size() <=
            tcp_json_max_frame_bytes;
    } catch (...) {
        output.clear();
        return false;
    }
}

void encode_tcp_frame(
    std::string_view payload,
    std::string& output) {

    const auto length =
        static_cast<std::uint32_t>(
            payload.size());

    output.resize(
        4 + payload.size());

    output[0] =
        static_cast<char>(
            (length >> 24u) & 0xFFu);

    output[1] =
        static_cast<char>(
            (length >> 16u) & 0xFFu);

    output[2] =
        static_cast<char>(
            (length >> 8u) & 0xFFu);

    output[3] =
        static_cast<char>(
            length & 0xFFu);

    output.replace(
        4,
        payload.size(),
        payload);
}

std::uint32_t decode_tcp_frame_length(
    const std::byte* bytes) noexcept {

    return
        (std::to_integer<std::uint32_t>(
             bytes[0]) << 24u) |
        (std::to_integer<std::uint32_t>(
             bytes[1]) << 16u) |
        (std::to_integer<std::uint32_t>(
             bytes[2]) << 8u) |
        std::to_integer<std::uint32_t>(
            bytes[3]);
}

}
