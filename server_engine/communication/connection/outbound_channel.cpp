#include "outbound_channel.hpp"

#include <utility>

namespace cw::server {

outbound_channel::outbound_channel(
    std::size_t byte_limit) noexcept
    : limit(byte_limit) {
}

bool outbound_channel::enqueue(
    outbound_message message) {

    std::unique_lock lock(mutex);

    if (is_closing) {
        return false;
    }

    if (message.estimated_bytes > limit ||
        bytes > limit - message.estimated_bytes) {

        is_closing = true;
        queue.clear();
        bytes = 0;

        lock.unlock();
        ready.notify_all();
        written.notify_all();
        return false;
    }

    bytes += message.estimated_bytes;
    queue.push_back(std::move(message));

    lock.unlock();
    ready.notify_one();
    return true;
}

bool outbound_channel::wait_next(
    outbound_write& output) {

    std::unique_lock lock(mutex);

    ready.wait(lock, [this] {
        return is_closing || !queue.empty();
    });

    if (is_closing) {
        return false;
    }

    output.connection_sequence =
        next_sequence++;

    output.message =
        std::move(queue.front());

    bytes -=
        output.message.estimated_bytes;

    queue.pop_front();
    return true;
}

bool outbound_channel::try_next(
    outbound_write& output) {

    std::lock_guard lock(mutex);

    if (is_closing || queue.empty()) {
        return false;
    }

    output.connection_sequence =
        next_sequence++;

    output.message =
        std::move(queue.front());

    bytes -=
        output.message.estimated_bytes;

    queue.pop_front();
    return true;
}

void outbound_channel::mark_written(
    std::uint64_t connection_sequence) noexcept {

    {
        std::lock_guard lock(mutex);

        if (connection_sequence > last_written) {
            last_written = connection_sequence;
        }
    }

    written.notify_all();
}

bool outbound_channel::wait_until_written(
    std::uint64_t connection_sequence,
    std::chrono::milliseconds timeout) {

    std::unique_lock lock(mutex);

    written.wait_for(
        lock,
        timeout,
        [this, connection_sequence] {
            return last_written >= connection_sequence ||
                is_closing;
        });

    return last_written >= connection_sequence;
}

bool outbound_channel::close() noexcept {
    {
        std::lock_guard lock(mutex);

        if (is_closing) {
            return false;
        }

        is_closing = true;
        queue.clear();
        bytes = 0;
    }

    ready.notify_all();
    written.notify_all();
    return true;
}

bool outbound_channel::closing() const noexcept {
    std::lock_guard lock(mutex);
    return is_closing;
}

std::size_t outbound_channel::queued_bytes() const noexcept {
    std::lock_guard lock(mutex);
    return bytes;
}

}
