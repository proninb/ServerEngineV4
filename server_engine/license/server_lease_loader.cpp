#include "server_lease_loader.hpp"

#include "../diagnostics/diagnostic_builder.hpp"
#include "../diagnostics/diagnostic_descriptor.hpp"
#include "../json/json_parser.hpp"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

namespace cw::server {
namespace {

enum class lease_field : std::uint8_t {
    none = 0,
    version,
    lease_id,
    not_before,
    expires_at,
    max_connections,
};

struct parse_state final {
    bool root_started = false;
    bool root_completed = false;
    bool valid = true;
    std::uint8_t seen = 0;
    lease_field field = lease_field::none;
    std::uint32_t version = 0;
    std::string lease_id;
    std::string not_before;
    std::string expires_at;
    std::uint32_t max_connections = 0;
    std::string detail;
};

[[nodiscard]] constexpr std::uint8_t field_bit(
    lease_field field) noexcept {

    const auto value = static_cast<std::uint8_t>(field);
    return value == 0
        ? 0
        : static_cast<std::uint8_t>(1u << (value - 1u));
}

[[nodiscard]] bool parse_two(
    std::string_view text,
    std::size_t offset,
    unsigned& output) noexcept {

    if (offset + 2 > text.size()) {
        return false;
    }

    const auto a = text[offset];
    const auto b = text[offset + 1];

    if (a < '0' || a > '9' ||
        b < '0' || b > '9') {
        return false;
    }

    output =
        static_cast<unsigned>(a - '0') * 10u +
        static_cast<unsigned>(b - '0');

    return true;
}

[[nodiscard]] bool parse_four(
    std::string_view text,
    std::size_t offset,
    int& output) noexcept {

    if (offset + 4 > text.size()) {
        return false;
    }

    output = 0;

    for (std::size_t index = 0; index < 4; ++index) {
        const auto ch = text[offset + index];

        if (ch < '0' || ch > '9') {
            return false;
        }

        output =
            output * 10 +
            static_cast<int>(ch - '0');
    }

    return true;
}

[[nodiscard]] bool parse_utc_datetime(
    std::string_view text,
    std::chrono::system_clock::time_point& output) noexcept {

    if (text.size() != 20 ||
        text[4] != '-' ||
        text[7] != '-' ||
        text[10] != 'T' ||
        text[13] != ':' ||
        text[16] != ':' ||
        text[19] != 'Z') {

        return false;
    }

    int year_value = 0;
    unsigned month_value = 0;
    unsigned day_value = 0;
    unsigned hour_value = 0;
    unsigned minute_value = 0;
    unsigned second_value = 0;

    if (!parse_four(text, 0, year_value) ||
        !parse_two(text, 5, month_value) ||
        !parse_two(text, 8, day_value) ||
        !parse_two(text, 11, hour_value) ||
        !parse_two(text, 14, minute_value) ||
        !parse_two(text, 17, second_value) ||
        hour_value > 23 ||
        minute_value > 59 ||
        second_value > 59) {

        return false;
    }

    const std::chrono::year_month_day date{
        std::chrono::year{year_value},
        std::chrono::month{month_value},
        std::chrono::day{day_value},
    };

    if (!date.ok()) {
        return false;
    }

    output =
        std::chrono::sys_days{date} +
        std::chrono::hours{hour_value} +
        std::chrono::minutes{minute_value} +
        std::chrono::seconds{second_value};

    return true;
}

class handler final : public json_event_handler {
public:
    explicit handler(
        parse_state& state)
        : state(state) {
    }

    void object_begin() override {
        if (!state.valid) {
            return;
        }

        if (!state.root_started) {
            state.root_started = true;
            return;
        }

        fail("server.lease fields must be scalar values");
    }

    void object_end() override {
        if (!state.valid) {
            return;
        }

        if (!state.root_started ||
            state.root_completed) {

            fail("server.lease must contain exactly one root object");
            return;
        }

        state.root_completed = true;
    }

    void array_begin() override {
        fail("server.lease does not support arrays");
    }

    void array_end() override {
        fail("server.lease does not support arrays");
    }

    void key(
        std::string_view key) override {

        if (!state.valid) {
            return;
        }

        lease_field field = lease_field::none;

        if (key == "version") {
            field = lease_field::version;
        } else if (key == "lease_id") {
            field = lease_field::lease_id;
        } else if (key == "not_before") {
            field = lease_field::not_before;
        } else if (key == "expires_at") {
            field = lease_field::expires_at;
        } else if (key == "max_connections") {
            field = lease_field::max_connections;
        } else {
            fail("unknown server.lease property: " + std::string(key));
            return;
        }

        const auto bit = field_bit(field);

        if ((state.seen & bit) != 0) {
            fail("duplicate server.lease property: " + std::string(key));
            return;
        }

        state.seen |= bit;
        state.field = field;
    }

    void value(
        json_value_view value) override {

        if (!state.valid ||
            state.field == lease_field::none) {
            return;
        }

        switch (state.field) {
        case lease_field::version:
            if (!value.get(state.version)) {
                fail("server.lease version must be an unsigned integer");
            }
            break;

        case lease_field::lease_id:
            if (!value.get(state.lease_id) ||
                state.lease_id.empty()) {
                fail("server.lease lease_id must be a non-empty string");
            }
            break;

        case lease_field::not_before:
            if (!value.get(state.not_before)) {
                fail("server.lease not_before must be a UTC datetime string");
            }
            break;

        case lease_field::expires_at:
            if (!value.get(state.expires_at)) {
                fail("server.lease expires_at must be a UTC datetime string");
            }
            break;

        case lease_field::max_connections:
            if (!value.get(state.max_connections)) {
                fail("server.lease max_connections must be an unsigned integer");
            }
            break;

        case lease_field::none:
            break;
        }

        state.field = lease_field::none;
    }

private:
    void fail(
        std::string detail) {

        if (!state.valid) {
            return;
        }

        state.valid = false;
        state.detail = std::move(detail);
    }

    parse_state& state;
};

[[nodiscard]] bool has_field(
    const parse_state& state,
    lease_field field) noexcept {

    return
        (state.seen &
         field_bit(field)) != 0;
}

void emit(
    diagnostic_collection& diagnostics,
    const diagnostic_descriptor& descriptor,
    operation_id operation,
    std::string detail) {

    diagnostics.emit(
        diagnostic(
            descriptor,
            operation)
            .detail(std::move(detail))
            .build());
}

}

server_status load_server_lease(
    const std::filesystem::path& path,
    std::chrono::system_clock::time_point now,
    const server_license& license,
    operation_id operation,
    diagnostic_collection& diagnostics,
    server_lease& output) {

    output.clear();

    if (!license.loaded()) {
        emit(
            diagnostics,
            diagnostics::server_lease_invalid,
            operation,
            "server.license must be loaded before server.lease");

        return server_status::server_lease_invalid;
    }

    std::ifstream stream(
        path,
        std::ios::binary);

    if (!stream) {
        emit(
            diagnostics,
            diagnostics::server_lease_read_failed,
            operation,
            path.string());

        return server_status::server_lease_read_failed;
    }

    std::string text{
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{},
    };

    if (!stream.good() &&
        !stream.eof()) {

        emit(
            diagnostics,
            diagnostics::server_lease_read_failed,
            operation,
            path.string());

        return server_status::server_lease_read_failed;
    }

    parse_state state;
    handler parser{state};

    const auto parsed =
        parse_json(
            text,
            parser);

    if (!parsed.ok()) {
        emit(
            diagnostics,
            diagnostics::server_lease_invalid,
            operation,
            std::string{
                json_error_message(parsed.code)});

        return server_status::server_lease_invalid;
    }

    if (!state.valid) {
        emit(
            diagnostics,
            diagnostics::server_lease_invalid,
            operation,
            state.detail);

        return server_status::server_lease_invalid;
    }

    if (!state.root_started ||
        !state.root_completed ||
        !has_field(state, lease_field::version) ||
        !has_field(state, lease_field::lease_id) ||
        !has_field(state, lease_field::not_before) ||
        !has_field(state, lease_field::expires_at) ||
        !has_field(state, lease_field::max_connections)) {

        emit(
            diagnostics,
            diagnostics::server_lease_invalid,
            operation,
            "server.lease requires version, lease_id, not_before, expires_at, and max_connections");

        return server_status::server_lease_invalid;
    }

    if (state.version != 1) {
        emit(
            diagnostics,
            diagnostics::server_lease_invalid,
            operation,
            "unsupported server.lease version; expected version 1");

        return server_status::server_lease_invalid;
    }

    if (state.max_connections == 0 ||
        state.max_connections >
            license.limits().max_connections) {

        emit(
            diagnostics,
            diagnostics::server_lease_invalid,
            operation,
            "server.lease max_connections must be greater than zero and cannot exceed server.license");

        return server_status::server_lease_invalid;
    }

    std::chrono::system_clock::time_point not_before;
    std::chrono::system_clock::time_point expiration;

    if (!parse_utc_datetime(
            state.not_before,
            not_before) ||
        !parse_utc_datetime(
            state.expires_at,
            expiration)) {

        emit(
            diagnostics,
            diagnostics::server_lease_invalid,
            operation,
            "server.lease times must use YYYY-MM-DDTHH:MM:SSZ UTC format");

        return server_status::server_lease_invalid;
    }

    if (not_before >= expiration) {
        emit(
            diagnostics,
            diagnostics::server_lease_invalid,
            operation,
            "server.lease not_before must be earlier than expires_at");

        return server_status::server_lease_invalid;
    }

    if (expiration >
        license.expires_at()) {

        emit(
            diagnostics,
            diagnostics::server_lease_invalid,
            operation,
            "server.lease cannot outlive server.license");

        return server_status::server_lease_invalid;
    }

    if (now < not_before) {
        emit(
            diagnostics,
            diagnostics::server_lease_not_active,
            operation,
            state.not_before);

        return server_status::server_lease_not_active;
    }

    if (now >= expiration) {
        emit(
            diagnostics,
            diagnostics::server_lease_expired,
            operation,
            state.expires_at);

        return server_status::server_lease_expired;
    }

    server_limits limits;
    limits.max_connections =
        state.max_connections;

    output.assign(
        std::move(state.lease_id),
        not_before,
        expiration,
        limits);

    return server_status::success;
}

}
