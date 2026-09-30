/*
 * Console command parser and endpoint thread.
 *
 * Current commands:
 *   LOAD <project-path>
 *   PUBLISH <project-path>
 *   BUILD <project-path>
 *   UNLOAD
 *   REBUILD <project-path>
 *   GET_STATE
 *   GET_VALUE <qualified-object[.member...]>
 *   SNAP_IC [/options=0] <ic-path>
 *   RESET_IC [/options=<0..7>] <ic-path>
 *   DELETE_IC <ic-path>
 *   LIST_IC
 *   RUN
 *   FREEZE
 *   SHUTDOWN
 *   EXIT        (alias of SHUTDOWN)
 *
 * Parsing is intentionally small at this architecture step. Successful parses
 * publish transport-neutral server_request values to request_queue.
 */

#include "server_console.hpp"

#include "../../diagnostics/diagnostic_formatter.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <iostream>
#include <sstream>

namespace cw::server {
namespace {

[[nodiscard]] std::string uppercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

struct parsed_ic_arguments final {
    std::filesystem::path path;
    std::uint32_t options = 0;
};

[[nodiscard]] bool parse_ic_arguments(
    std::istringstream& stream,
    bool allow_options,
    std::uint32_t max_options,
    parsed_ic_arguments& output) {

    output = {};

    std::string first;
    if (!(stream >> first)) {
        return false;
    }

    constexpr std::string_view options_prefix = "/options=";
    const auto view = std::string_view(first);

    if (view.starts_with(options_prefix)) {
        if (!allow_options) {
            return false;
        }

        const auto value_text =
            view.substr(
                options_prefix.size());

        std::uint32_t value = 0;

        const auto parsed =
            std::from_chars(
                value_text.data(),
                value_text.data() + value_text.size(),
                value);

        if (value_text.empty() ||
            parsed.ec != std::errc{} ||
            parsed.ptr != value_text.data() + value_text.size() ||
            value > max_options) {

            return false;
        }

        output.options = value;

        std::string path;
        std::getline(
            stream >> std::ws,
            path);

        if (path.empty()) {
            return false;
        }

        output.path = std::move(path);
        return true;
    }

    std::string remainder;
    std::getline(stream, remainder);

    first += remainder;

    if (first.empty()) {
        return false;
    }

    output.path = std::move(first);
    return true;
}


}

server_console::~server_console() {
    stop();
}

bool server_console::start(request_queue& queue) {
    if (running.exchange(true)) {
        return true;
    }

    if (!input.open()) {
        running = false;
        return false;
    }

    requests = &queue;
    thread = std::jthread([this] { run(); });
    return true;
}

void server_console::stop() noexcept {
    if (!running.exchange(false)) {
        return;
    }

    input.interrupt();

    if (thread.joinable()) {
        thread.join();
    }

    input.close();
    requests = nullptr;
}

void server_console::run() {
    std::string line;

    while (running && input.read_line(line)) {
        std::istringstream stream(line);
        std::string verb;
        stream >> verb;

        if (verb.empty()) {
            continue;
        }

        verb = uppercase(std::move(verb));

        if (verb == "LOAD") {
            std::string path;
            std::getline(stream >> std::ws, path);

            if (path.empty()) {
                std::cout << "LOAD requires a project path\n";
                continue;
            }

            publish({server_request_kind::load, std::move(path)});
        } else if (verb == "PUBLISH") {
            std::string path;
            std::getline(stream >> std::ws, path);

            if (path.empty()) {
                std::cout << "PUBLISH requires a project path\n";
                continue;
            }

            publish({server_request_kind::publish, std::move(path)});
        } else if (verb == "BUILD") {
            std::string path;
            std::getline(stream >> std::ws, path);

            if (path.empty()) {
                std::cout << "BUILD requires a project path\n";
                continue;
            }

            publish({server_request_kind::build, std::move(path)});
        } else if (verb == "UNLOAD") {
            publish({server_request_kind::unload, {}});
        } else if (verb == "REBUILD") {
            std::string path;
            std::getline(stream >> std::ws, path);

            if (path.empty()) {
                std::cout << "REBUILD requires a project path\n";
                continue;
            }

            publish({server_request_kind::rebuild, std::move(path)});
        } else if (verb == "GET_STATE") {
            publish({
                server_request_kind::get_state,
                {},
                {},
            });
        } else if (verb == "SNAP_IC") {
            parsed_ic_arguments parsed;

            if (!parse_ic_arguments(stream, true, 0, parsed)) {
                std::cout
                    << "SNAP_IC requires [/options=0] <ic-path>\n";
                continue;
            }

            server_request request;
            request.kind = server_request_kind::snap_ic;
            request.path = std::move(parsed.path);
            request.snap_options =
                static_cast<snap_ic_options>(parsed.options);
            publish(std::move(request));
        } else if (verb == "RESET_IC") {
            parsed_ic_arguments parsed;

            if (!parse_ic_arguments(
                    stream,
                    true,
                    reset_ic_options_mask,
                    parsed)) {

                std::cout
                    << "RESET_IC requires [/options=<0..7>] <ic-path>\n";
                continue;
            }

            server_request request;
            request.kind = server_request_kind::reset_ic;
            request.path = std::move(parsed.path);
            request.reset_options =
                static_cast<reset_ic_options>(parsed.options);
            publish(std::move(request));
        } else if (verb == "DELETE_IC") {
            parsed_ic_arguments parsed;

            if (!parse_ic_arguments(stream, false, 0, parsed)) {
                std::cout
                    << "DELETE_IC requires <ic-path>\n";
                continue;
            }

            server_request request;
            request.kind = server_request_kind::delete_ic;
            request.path = std::move(parsed.path);
            publish(std::move(request));
        } else if (verb == "LIST_IC") {
            publish({server_request_kind::list_ic, {}});
        } else if (verb == "RUN") {
            publish({
                server_request_kind::run,
                {},
                {},
            });
        } else if (verb == "FREEZE") {
            publish({
                server_request_kind::freeze,
                {},
                {},
            });
        } else if (verb == "GET_VALUE") {
            std::string name;
            std::getline(
                stream >> std::ws,
                name);

            if (name.empty()) {
                std::cout
                    << "GET_VALUE requires a Runtime name\n";
                continue;
            }

            publish({
                server_request_kind::get_value,
                {},
                std::move(name),
            });
        } else if (verb == "SHUTDOWN" || verb == "EXIT") {
            publish({server_request_kind::shutdown, {}});
            return;
        } else {
            std::cout << "Unknown command: " << verb << '\n';
        }
    }
}

void server_console::publish(
    server_request value) {

    server_request_message message;
    message.request =
        std::move(value);

    message.identity.origin =
        request_origin_kind::console;

    message.origin =
        server_request_origin{
            this,
            &server_console::present_result,
        };

    (void)requests->push(
        std::move(message));
}

void server_console::present_result(
    void* context,
    request_id,
    const server_response& result) {

    static_cast<server_console*>(context)
        ->present(result);
}

void server_console::present(
    const server_response& result) {

    if (!result.diagnostics.empty()) {
        format_diagnostics(
            std::cerr,
            result.diagnostics);
    }

    if (!succeeded(result.status)) {
        std::cerr
            << "Request failed: "
            << static_cast<int>(result.status)
            << '\n';

        return;
    }

    switch (result.payload) {
    case server_response_payload_kind::none:
        break;

    case server_response_payload_kind::state:
        std::cout
            << "STATE "
            << (result.state.project == project_state::unloaded
                ? "UNLOADED"
                : result.state.project == project_state::loaded
                    ? "LOADED"
                    : result.state.project == project_state::run
                        ? "RUN"
                        : result.state.project == project_state::freeze
                            ? "FREEZE"
                            : result.state.project == project_state::resetting_ic
                                ? "RESETTING_IC"
                                : "SNAPPING_IC")
            << '\n';
        break;

    case server_response_payload_kind::runtime_value:
        std::cout
            << "VALUE type="
            << static_cast<unsigned>(
                result.value.type)
            << " size="
            << static_cast<unsigned>(
                result.value.size)
            << " bits="
            << result.value.bits
            << '\n';
        break;

    case server_response_payload_kind::ic_catalog:
        for (const auto& entry : result.catalog.items) {
            std::cout
                << entry.path.generic_string()
                << '\n';
        }
        break;
    }
}

}
