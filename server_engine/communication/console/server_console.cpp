/*
 * Console command parser and endpoint thread.
 *
 * Current commands:
 *   LOAD <project-path>
 *   BUILD <project-path>
 *   UNLOAD
 *   REBUILD <project-path>
 *   SHUTDOWN
 *   EXIT        (alias of SHUTDOWN)
 *
 * Parsing is intentionally small at this architecture step. Successful parses
 * publish transport-neutral server_command values to command_queue.
 */

#include "server_console.hpp"

#include "../../diagnostics/diagnostic_formatter.hpp"

#include <algorithm>
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

}

server_console::~server_console() {
    stop();
}

bool server_console::start(command_queue& queue) {
    if (running.exchange(true)) {
        return true;
    }

    if (!input.open()) {
        running = false;
        return false;
    }

    commands = &queue;
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
    commands = nullptr;
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

            publish({server_command_kind::load, std::move(path)});
        } else if (verb == "BUILD") {
            std::string path;
            std::getline(stream >> std::ws, path);

            if (path.empty()) {
                std::cout << "BUILD requires a project path\n";
                continue;
            }

            publish({server_command_kind::build, std::move(path)});
        } else if (verb == "UNLOAD") {
            publish({server_command_kind::unload, {}});
        } else if (verb == "REBUILD") {
            std::string path;
            std::getline(stream >> std::ws, path);

            if (path.empty()) {
                std::cout << "REBUILD requires a project path\n";
                continue;
            }

            publish({server_command_kind::rebuild, std::move(path)});
        } else if (verb == "SHUTDOWN" || verb == "EXIT") {
            publish({server_command_kind::shutdown, {}});
            return;
        } else {
            std::cout << "Unknown command: " << verb << '\n';
        }
    }
}

void server_console::publish(
    server_command command) {

    server_command_request request;
    request.command =
        std::move(command);

    request.origin =
        server_command_origin{
            this,
            &server_console::present_result,
        };

    commands->push(
        std::move(request));
}

void server_console::present_result(
    void* context,
    const server_command_result& result) {

    static_cast<server_console*>(context)
        ->present(result);
}

void server_console::present(
    const server_command_result& result) {

    if (!result.diagnostics.empty()) {
        format_diagnostics(
            std::cerr,
            result.diagnostics);
    }

    if (!succeeded(result.status)) {
        std::cerr
            << "Command failed: "
            << static_cast<int>(result.status)
            << '\n';
    }
}

}
