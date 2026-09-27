/*
 * Console command parser and endpoint thread.
 *
 * Current commands:
 *   LOAD <project-path>
 *   PUBLISH <project-path>
 *   BUILD <project-path>
 *   UNLOAD
 *   REBUILD <project-path>
 *   SHUTDOWN
 *   EXIT        (alias of SHUTDOWN)
 *
 * Parsing is intentionally small at this architecture step. Successful parses
 * publish transport-neutral server_request values to request_queue.
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

    message.origin =
        server_request_origin{
            this,
            &server_console::present_result,
        };

    requests->push(
        std::move(message));
}

void server_console::present_result(
    void* context,
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
            << "Command failed: "
            << static_cast<int>(result.status)
            << '\n';
    }
}

}
