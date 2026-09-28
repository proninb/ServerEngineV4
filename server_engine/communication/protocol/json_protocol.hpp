/*
 * TCP JSON protocol codec.
 *
 * The codec translates between UTF-8 JSON payloads and transport-neutral
 * Communication/Server messages. TCP framing is handled separately.
 */
#pragma once

#include "../connection/outbound_channel.hpp"
#include "../server_request.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace cw::server {

inline constexpr std::size_t tcp_json_max_frame_bytes =
    1024u * 1024u;

inline constexpr std::size_t tcp_json_outbound_budget_bytes =
    4u * 1024u * 1024u;

enum class json_request_kind : std::uint8_t {
    login = 0,
    server,
    client,
};

struct json_login_request final {
    std::string name;
};

struct json_client_request final {
    client_message message;
};

using json_request_payload =
    std::variant<
        json_login_request,
        server_request,
        json_client_request>;

struct json_request final {
    request_id request;
    json_request_kind kind = json_request_kind::server;
    json_request_payload payload = server_request{};
};

enum class json_protocol_error : std::uint8_t {
    none = 0,
    invalid_json,
    invalid_schema,
    unsupported_command,
};

struct json_decode_result final {
    json_protocol_error error = json_protocol_error::none;

    [[nodiscard]] constexpr bool ok() const noexcept {
        return error == json_protocol_error::none;
    }
};

[[nodiscard]] json_decode_result decode_json_request(
    std::string_view text,
    json_request& output) noexcept;

[[nodiscard]] bool encode_json_outbound(
    const outbound_write& message,
    std::string& output) noexcept;

// Four-byte unsigned big-endian payload length.
void encode_tcp_frame(
    std::string_view payload,
    std::string& output);

[[nodiscard]] std::uint32_t decode_tcp_frame_length(
    const std::byte* bytes) noexcept;

}
