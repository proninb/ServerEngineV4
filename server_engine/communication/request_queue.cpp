/*
 * request_queue implementation.
 *
 * The mutex protects only control-plane request traffic. Runtime execution
 * must never depend on this queue.
 */
#include "request_queue.hpp"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>
#include <variant>

namespace cw::server {

namespace {

void add_bytes(
    std::size_t value,
    std::size_t& total) noexcept {

    const auto maximum =
        (std::numeric_limits<std::size_t>::max)();

    total =
        value > maximum - total
        ? maximum
        : total + value;
}

void add_elements(
    std::size_t count,
    std::size_t size,
    std::size_t& total) noexcept {

    if (count == 0 ||
        size == 0) {

        return;
    }

    const auto maximum =
        (std::numeric_limits<std::size_t>::max)();

    if (count > maximum / size) {
        total = maximum;
        return;
    }

    add_bytes(
        count * size,
        total);
}

}

std::size_t request_queue::estimate_message_bytes(
    const communication_control_message& message) noexcept {

    std::size_t total =
        sizeof(communication_control_message);

    if (const auto* request =
            std::get_if<server_request_message>(
                &message)) {

        const auto& native =
            request->request.path.native();

        add_elements(
            native.size(),
            sizeof(
                std::filesystem::path::value_type),
            total);

        add_bytes(
            request->request.name.size(),
            total);

        add_bytes(
            request->request.type.size(),
            total);

        add_elements(
            request->request.objects.size(),
            sizeof(std::string),
            total);

        for (const auto& object :
             request->request.objects) {

            add_bytes(
                object.size(),
                total);
        }

        add_bytes(
            request->identity.name.size(),
            total);

        return total;
    }

    if (const auto* login =
            std::get_if<login_control_message>(
                &message)) {

        add_bytes(
            login->name.size(),
            total);

        return total;
    }

    if (const auto* client =
            std::get_if<client_control_message>(
                &message)) {

        add_bytes(
            client->message.login.size(),
            total);

        add_bytes(
            client->message.arg.size(),
            total);

        add_bytes(
            client->message.parameters.size(),
            total);

        return total;
    }

    return total;
}

request_queue_push_result request_queue::try_push(
    communication_control_message request) {

    const auto close_control =
        std::holds_alternative<
            connection_close_control_message>(
                request);

    const auto bytes =
        close_control
        ? 0
        : estimate_message_bytes(
            request);

    {
        std::lock_guard lock(mutex);

        if (!accepting) {
            return request_queue_push_result::
                stopped;
        }

        if (!close_control &&
            (accounted_messages >=
                 limits.messages ||
             bytes >
                 limits.bytes -
                    (std::min)(
                        accounted_bytes,
                        limits.bytes))) {

            return request_queue_push_result::
                overloaded;
        }

        try {
            queue.push({
                std::move(request),
                bytes,
                !close_control,
            });
        }
        catch (...) {
            return request_queue_push_result::
                failed;
        }

        if (!close_control) {
            ++accounted_messages;
            accounted_bytes += bytes;
        }
    }

    condition.notify_one();

    return request_queue_push_result::
        accepted;
}

bool request_queue::push(
    communication_control_message request) {

    return try_push(
               std::move(request)) ==
        request_queue_push_result::
            accepted;
}

void request_queue::stop_accepting_and_discard() noexcept {
    std::queue<queued_message> discarded;

    {
        std::lock_guard lock(mutex);
        accepting = false;
        accounted_messages = 0;
        accounted_bytes = 0;
        discarded.swap(queue);
    }

    condition.notify_all();
}

bool request_queue::wait_pop(
    communication_control_message& output) {

    std::unique_lock lock(mutex);

    condition.wait(lock, [this] {
        return !queue.empty() || !accepting;
    });

    if (queue.empty()) {
        return false;
    }

    auto& current =
        queue.front();

    output =
        std::move(
            current.message);

    if (current.accounted) {
        --accounted_messages;
        accounted_bytes -=
            current.accounted_bytes;
    }

    queue.pop();
    return true;
}

bool request_queue::wait_pop_until(
    std::chrono::system_clock::time_point deadline,
    communication_control_message& output) {

    std::unique_lock lock(mutex);

    if (!condition.wait_until(
            lock,
            deadline,
            [this] {
                return !queue.empty() || !accepting;
            })) {

        return false;
    }

    if (queue.empty()) {
        return false;
    }

    auto& current =
        queue.front();

    output =
        std::move(
            current.message);

    if (current.accounted) {
        --accounted_messages;
        accounted_bytes -=
            current.accounted_bytes;
    }

    queue.pop();
    return true;
}

}
